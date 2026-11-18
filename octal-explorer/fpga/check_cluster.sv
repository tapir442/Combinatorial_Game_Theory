// =============================================================================
// check_cluster.sv -- NS lane groups with their memories: a complete split check
// except for the job builder and the fallback for deep heaps
//
// STATUS: for resource and clock estimates (synth_check.tcl) and the functional
// test tb_check_cluster.sv.  Nothing instantiates it yet.
//
// -----------------------------------------------------------------------------
// What it does
// -----------------------------------------------------------------------------
// A job is GS consecutive heaps n0 .. n0+GS-1 with their speculated values g and
// R(g) (the number of rare-class values below g).  An idle group takes it,
// scans a = 0, 1, ..., LIM-1 with check_group_lr's lanes (stopping early when
// all lanes are done) and reports one result: n0 and the lanes that were NOT
// done -- heaps whose witness depth is >= LIM, for the fallback.  (a = 0 pairs
// with G(0) = 0, a rare value; the group's rare-G(a) filter skips it.)
//
// Precondition, as for check_group_lr: n0 >= 100,000.  And the window ring must
// hold G(n0 + GS - 2 - LIM - 16 .. n0 + GS - 2) while the job runs: the job
// builder issues jobs only for written values, and KW must exceed LIM plus
// the heaps in flight (NS * GS) plus 32 -- see tb_check_cluster.sv.
//
// -----------------------------------------------------------------------------
// Memories (block RAM, words of 16 values = 144 bits)
// -----------------------------------------------------------------------------
//   prefix  G(0 .. KP-1), written once through pw_* (9 bits at a time)
//   window  ring of the last KW values, G(n) at n mod KW, written through ww_*
//           as the speculation core produces them
// Each has one read port, shared by the NS groups in fixed time slots: slot s
// (a counter 0 .. NS-1) reads the word that group s asks for, if it asks.  A
// group thus gets a word every NS clocks; it consumes one value per clock, so
// NS <= 13 keeps the streams from running dry (check_stream.sv).  The read has
// 3 clocks of latency: address register, RAM, RAM output register.
//
// -----------------------------------------------------------------------------
// One group
// -----------------------------------------------------------------------------
// Two streams (check_stream): the prefix G(0), G(1), ... and the window
// downwards from top = m0 + GS - 1 = n0 + GS - 2.  Lane l needs G(m0 + l - a):
// a shift register `sr` of GS window values gives win[j] = G(top - a - j).
//   IDLE  all lanes ready: take a job (lanes load at once, streams restart);
//         the job is latched one clock after job_v / job_rdy and starts the
//         group the clock after that
//   FILL  shift skip + GS window values into sr; skip = the values above top in
//         top's word
//   SCAN  step whenever both heads are valid; a = 0 .. LIM-1, or until all done
//   WAIT  7 clocks for the lane pipeline (done is 4 behind), then latch the not-done lanes and
//         abort them (they clear their seen sets like a done lane)
//   REP   offer the result; the lowest-numbered reporting group goes first
// =============================================================================
`default_nettype none
module check_cluster #(
    parameter int GS  = 16,                // lanes per group
    parameter int NS  = 8,                 // groups (<= 13)
    parameter int KP  = 16384,             // prefix values in BRAM
    parameter int KW  = 32768,             // window ring values
    parameter int LIM = 16384              // pairs scanned per heap at most (<= KP)
) (
    input  wire               clk,
    // ---- memory writes ----
    input  wire               pw_en,
    input  wire [$clog2(KP)-1:0] pw_a,
    input  wire [8:0]         pw_v,
    input  wire               ww_en,
    input  wire [$clog2(KW)-1:0] ww_a,     // n mod KW
    input  wire [8:0]         ww_v,
    // ---- jobs ----
    input  wire               job_v,       // a job is offered (taken when job_rdy)
    output wire               job_rdy,
    input  wire [47:0]        job_n0,
    input  wire [GS*9-1:0]    job_g,
    input  wire [GS*9-1:0]    job_r,
    // ---- results ----
    output reg                res_v = 0,
    output reg  [47:0]        res_n0 = 0,
    output reg  [GS-1:0]      res_fail = 0 // lane l: heap n0 + l not settled
);
    localparam int PWD = KP / 16, WWD = KW / 16;          // words
    localparam int APW = $clog2(PWD), AWW = $clog2(WWD);
    localparam int SW  = NS > 1 ? $clog2(NS) : 1;
    localparam int LAT = 3;

    // =========================================================================
    // Memories
    // =========================================================================
    (* ram_style = "block" *) reg [143:0] pmem [0:PWD-1];
    (* ram_style = "block" *) reg [143:0] wmem [0:WWD-1];

    // writes: one 9-bit value into its slot of a word (byte enables of 9 bits)
    always_ff @(posedge clk)
        for (int i = 0; i < 16; i++)
            if (pw_en && pw_a[3:0] == i) pmem[pw_a[$clog2(KP)-1:4]][i*9 +: 9] <= pw_v;
    always_ff @(posedge clk)
        for (int i = 0; i < 16; i++)
            if (ww_en && ww_a[3:0] == i) wmem[ww_a[$clog2(KW)-1:4]][i*9 +: 9] <= ww_v;

    // reads: time slots
    reg  [SW-1:0]   slot = 0;
    always_ff @(posedge clk) slot <= (slot == NS - 1) ? '0 : slot + 1'b1;

    wire [NS-1:0]   p_req, w_req;
    wire [APW-1:0]  p_rw [NS];
    wire [AWW-1:0]  w_rw [NS];
    wire [NS-1:0]   p_gnt, w_gnt;
    for (genvar s = 0; s < NS; s++) begin : gnt_g
        assign p_gnt[s] = slot == s && p_req[s];
        assign w_gnt[s] = slot == s && w_req[s];
    end

    reg  [APW-1:0]  p_ra = 0;
    reg  [AWW-1:0]  w_ra = 0;
    reg             p_re = 0, w_re = 0;
    reg  [143:0]    p_dq = 0, p_dq2 = 0, w_dq = 0, w_dq2 = 0;
    reg  [NS-1:0]   p_gd [LAT], w_gd [LAT];               // grants, delayed
    always_ff @(posedge clk) begin
        p_ra <= p_rw[slot];  p_re <= p_req[slot];
        w_ra <= w_rw[slot];  w_re <= w_req[slot];
        if (p_re) p_dq <= pmem[p_ra];
        if (w_re) w_dq <= wmem[w_ra];
        p_dq2 <= p_dq;
        w_dq2 <= w_dq;
        p_gd[0] <= p_gnt;  w_gd[0] <= w_gnt;
        for (int i = 1; i < LAT; i++) begin p_gd[i] <= p_gd[i-1]; w_gd[i] <= w_gd[i-1]; end
    end

    // =========================================================================
    // Groups
    // =========================================================================
    typedef enum logic [2:0] {IDLE, FILL, SCAN, WAIT, REP} gst_t;
    wire [NS-1:0] idle, rep;
    wire [47:0]   g_n0   [NS];
    wire [GS-1:0] g_fail [NS];

    // job: to the lowest-numbered idle group.  Registered twice for timing:
    // idle_q (from the lanes' ready and the state), then the choice `take`
    // latches the job into job_*_q and pulses take_q, which starts the group.
    reg  [NS-1:0] idle_q = 0, take_q = 0;
    reg  [47:0]     job_n0_q = 0;
    reg  [GS*9-1:0] job_g_q = 0, job_r_q = 0;
    wire [NS-1:0] take;
    for (genvar s = 0; s < NS; s++) begin : take_g
        if (s == 0) assign take[s] = job_v && idle_q[0];
        else        assign take[s] = job_v && idle_q[s] && !(|idle_q[s-1:0]);
    end
    assign job_rdy = |idle_q;
    always_ff @(posedge clk) begin
        idle_q <= idle & ~take & ~take_q;              // a group just chosen is not idle
        take_q <= take;
        if (job_v && job_rdy) begin
            job_n0_q <= job_n0;
            job_g_q  <= job_g;
            job_r_q  <= job_r;
        end
    end

    // result: the lowest-numbered reporting group, in two stages: the choice
    // rsel (the group leaves REP on it) is registered, then its n0 / fail are
    // muxed out.  They stay put for that clock: a group needs at least three
    // clocks from IDLE to a new job (idle_q, take, take_q).
    wire [NS-1:0] rsel;
    for (genvar s = 0; s < NS; s++) begin : rsel_g
        if (s == 0) assign rsel[s] = rep[0];
        else        assign rsel[s] = rep[s] && !(|rep[s-1:0]);
    end
    reg [NS-1:0] rsel_q = 0;
    always_ff @(posedge clk) begin
        rsel_q <= rsel;
        res_v  <= |rsel_q;
        for (int s = 0; s < NS; s++)
            if (rsel_q[s]) begin res_n0 <= g_n0[s]; res_fail <= g_fail[s]; end
    end

    for (genvar s = 0; s < NS; s++) begin : grp
        gst_t           st = IDLE;
        reg  [47:0]     n0 = 0;
        reg  [GS-1:0]   fail = 0;
        reg  [$clog2(LIM)-1:0] a = 0;
        reg  [$clog2(GS+16):0] fc = 0;     // FILL count
        reg  [2:0]      wc = 0;            // WAIT count
        reg  [GS*9-1:0] sr = 0;            // window values: sr[j] = G(top - a - j)
        wire [GS-1:0]   done, ready;
        reg             abort = 0;         // with the result: free the lanes (registered)

        assign idle[s]   = st == IDLE && &ready;
        assign rep[s]    = st == REP;
        assign g_n0[s]   = n0;
        assign g_fail[s] = fail;

        // top = n0 + GS - 2 (mod KW): the window word and the values above top in it
        wire [$clog2(KW)-1:0] top = job_n0_q[$clog2(KW)-1:0] + ($clog2(KW))'(GS - 2);

        wire [8:0] p_head, w_head;
        wire       p_hv, w_hv;
        wire       stepping = st == SCAN && p_hv && w_hv;
        wire       w_adv    = (st == FILL && w_hv) || stepping;

        check_stream #(.AW(APW), .DESC(0)) u_p (
            .clk, .flush(take_q[s]), .start_w('0),
            .req(p_req[s]), .req_w(p_rw[s]), .gnt(p_gnt[s]),
            .rsp_v(p_gd[LAT-1][s]), .rsp_d(p_dq2),
            .adv(stepping), .head(p_head), .hv(p_hv));
        check_stream #(.AW(AWW), .DESC(1)) u_w (
            .clk, .flush(take_q[s]), .start_w(top[$clog2(KW)-1:4]),
            .req(w_req[s]), .req_w(w_rw[s]), .gnt(w_gnt[s]),
            .rsp_v(w_gd[LAT-1][s]), .rsp_d(w_dq2),
            .adv(w_adv), .head(w_head), .hv(w_hv));

        check_group_lr #(.GS(GS)) u_lanes (
            .clk, .start(take_q[s]), .ld_g(job_g_q), .ld_r(job_r_q), .abort,
            .step(stepping), .pre(p_head), .win(sr),
            .done, .ready);

        always_ff @(posedge clk) begin
            if (w_adv) sr <= {w_head, sr[GS*9-1:9]};      // newest at the top: sr[j] <- sr[j+1]
            abort <= st == WAIT && wc == 3'd5;            // high while wc == 6
            case (st)
                IDLE: if (take_q[s]) begin
                    n0 <= job_n0_q;
                    fc <= ($bits(fc))'(15 - top[3:0] + GS);
                    st <= FILL;
                end
                FILL: if (w_hv) begin
                    fc <= fc - 1'b1;
                    if (fc == 1) begin a <= 0; st <= SCAN; end
                end
                SCAN: begin
                    if (stepping) a <= a + 1'b1;
                    if ((stepping && a == LIM - 1) || &done) begin wc <= 0; st <= WAIT; end
                end
                WAIT: begin
                    wc <= wc + 1'b1;
                    if (wc == 3'd6) begin fail <= ~done; st <= REP; end
                end
                REP: if (rsel[s]) st <= IDLE;
                default: st <= IDLE;
            endcase
        end
    end
endmodule
`default_nettype wire
