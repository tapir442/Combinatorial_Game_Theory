// =============================================================================
// check_deep.sv -- the fallback of the split check: one deep lane on DDR2
//
// STATUS: functional test tb_check_deep.sv (with a model of the MIG native
// interface); not yet connected.  Runs in the MIG's user clock (81.25 MHz).
//
// -----------------------------------------------------------------------------
// What it does
// -----------------------------------------------------------------------------
// The clusters scan LIM = 16,384 pairs per heap; about 4.5e-5 of the heaps need
// more (check_top's fail list).  This lane checks such a heap n completely,
// from DDR2, the same way a cluster lane does: scan a = 0, 1, 2, ... with
// pairs G(a) ^ G(m - a) (m = n - 1), count the rare-class values below g = G(n)
// that have been seen (seen set in LUT-RAM, counter from R(g)), done at 0.
// About 500 heaps/s with ~30,000 pairs each need ~15 M pairs/s; the lane can
// do about one pair per clock.
//
// DDR2 (128 MiB; 128-bit words = 8 values, value i of a word in bits [16i +: 9]):
//   words 0 .. 2^LGP-1           prefix G(0 .. 8*2^LGP - 1), loaded by the host
//   words 2^LGR .. 2^(LGR+1)-1   ring: G(h) in word 2^LGR + (h/8 mod 2^LGR)
// Writes come in through w_*: ring words in heap order (w_n = heap of value
// 0, a multiple of 8), prefix words by address.
//
// Limits (a heap is never declared verified from values that are not there):
// the scan stops at amax = the smallest of
//   m / 2                        (pairs beyond repeat)
//   pre_len - 1                  (prefix loaded)
//   m - oldest                   oldest = the oldest ring value that is valid:
//                                max(n_lo, wr_upto - ring size + MARGIN)
// n_lo is the first heap written this session (the ring holds nothing older),
// and MARGIN (2^20 values) covers what the core writes during a long scan.  A
// heap not settled by amax is reported with r_ok = 0 (unresolved), never as
// verified.
//
// Precondition (as for check_group_lr): G(m - a) is common for all pairs, which
// holds for n >= 100,000; then the pair is rare-class iff G(a) is common.
// =============================================================================
`default_nettype none

// one read stream: words from DDR2 ascending or descending, one value per clock
module deep_stream #(
    parameter int AW   = 22,               // word address bits (within the region)
    parameter int QD   = 16,               // words buffered + in flight
    parameter bit DESC = 0
) (
    input  wire            clk,
    input  wire            flush,          // start at start_w; only with nothing in flight
    input  wire [AW-1:0]   start_w,
    input  wire            run,            // keep requesting
    output wire            req,
    output wire [AW-1:0]   req_w,
    input  wire            gnt,
    input  wire            rsp_v,
    input  wire [127:0]    rsp_d,
    input  wire            adv,
    output wire [8:0]      head,
    output wire            hv,
    output wire            busy            // reads in flight
);
    localparam int QB = $clog2(QD);
    localparam logic [2:0] FIRST = DESC ? 3'd7 : 3'd0;
    localparam logic [2:0] LAST  = DESC ? 3'd0 : 3'd7;

    (* ram_style = "distributed" *) reg [127:0] fifo [0:QD-1];
    reg [AW-1:0] wa = 0;
    reg [QB-1:0] wp = 0, rp = 0;
    reg [QB:0]   infl = 0, stored = 0;     // words in flight / in the FIFO
    reg [2:0]    ptr = 0;
    reg [8:0]    head_r = 0;
    reg          hv_r = 0;

    assign req   = run && (infl + stored) < (QB+1)'(QD);
    assign req_w = wa;
    assign head  = head_r;
    assign hv    = hv_r;
    assign busy  = infl != 0;

    wire load = adv || !hv_r;
    wire take = load && stored != 0;
    wire last = take && ptr == LAST;

    always_ff @(posedge clk) begin
        if (rsp_v) fifo[wp] <= rsp_d;
        if (take)  head_r   <= fifo[rp][ptr*16 +: 9];
    end

    always_ff @(posedge clk) begin
        if (flush) begin
            wa <= start_w; wp <= 0; rp <= 0; stored <= 0; ptr <= FIRST; hv_r <= 1'b0;
        end else begin
            if (gnt)   wa <= DESC ? wa - 1'b1 : wa + 1'b1;
            if (rsp_v) wp <= wp + 1'b1;
            infl   <= infl + gnt - rsp_v;
            stored <= stored + rsp_v - last;
            if (take) begin
                hv_r <= 1'b1;
                ptr  <= DESC ? ptr - 1'b1 : ptr + 1'b1;
                if (last) rp <= rp + 1'b1;
            end else if (load) hv_r <= 1'b0;
        end
    end
endmodule

module check_deep #(
    parameter int LGP    = 21,             // prefix words: 2^21 = 16 M values
    parameter int LGR    = 22,             // ring words:   2^22 = 32 M values
    parameter int QD     = 16,
    parameter int MARGIN = 1 << 20         // values (see the header)
) (
    input  wire               clk,
    input  wire               rst,
    // ---- writes ----
    input  wire               w_v,
    input  wire               w_ring,      // 1: ring word of heaps w_n .. w_n+7; 0: prefix word w_pa
    input  wire [47:0]        w_n,
    input  wire [LGP-1:0]     w_pa,
    input  wire [127:0]       w_d,
    output wire               w_pop,
    // ---- session ----
    input  wire [47:0]        n_lo,        // the ring holds heaps >= n_lo of this session
    input  wire [31:0]        pre_len,     // the prefix holds G(0 .. pre_len-1)
    // ---- jobs and results ----
    input  wire               j_v,
    input  wire [47:0]        j_n,
    output wire               j_pop,
    output reg                r_v = 0,
    output reg  [47:0]        r_n = 0,
    output reg                r_ok = 0,
    output reg  [47:0]        wr_upto = 0, // ring written for heaps < wr_upto
    // ---- MIG native interface ----
    output reg  [26:0]        app_addr = 0,
    output reg  [2:0]         app_cmd = 0,
    output reg                app_en = 0,
    input  wire               app_rdy,
    output reg  [127:0]       app_wdf_data = 0,
    output reg                app_wdf_wren = 0,
    output wire               app_wdf_end,
    output wire [15:0]        app_wdf_mask,
    input  wire               app_wdf_rdy,
    input  wire [127:0]       app_rd_data,
    input  wire               app_rd_data_valid
);
    import check_pkg::RB;
    import check_pkg::is_common;
    localparam longint RINGV = longint'(8) << LGR;   // values in the ring

    assign app_wdf_end  = app_wdf_wren;    // BL8 at 4:1: one data beat per write
    assign app_wdf_mask = 16'h0000;

    // =========================================================================
    // Streams and the DDR2 command arbiter
    // =========================================================================
    wire        p_req, w_req, p_gnt, w_gnt, p_hv, w_hv, p_busy, w_busy;
    wire [LGR-1:0] p_rw, w_rw;
    wire [8:0]  p_head, w_head;
    reg         s_flush = 0, s_run = 0;
    reg  [LGR-1:0] w_start = 0;
    wire        p_adv, w_adv;
    // read tags: which stream each read in flight belongs to (reads return in order)
    reg  [63:0] tag = 0;
    reg  [5:0]  tw = 0, tr = 0;
    wire        rtag = tag[tr];

    deep_stream #(.AW(LGR), .QD(QD), .DESC(0)) u_p (
        .clk, .flush(s_flush), .start_w('0), .run(s_run), .req(p_req), .req_w(p_rw), .gnt(p_gnt),
        .rsp_v(app_rd_data_valid && !rtag), .rsp_d(app_rd_data),
        .adv(p_adv), .head(p_head), .hv(p_hv), .busy(p_busy));
    deep_stream #(.AW(LGR), .QD(QD), .DESC(1)) u_w (
        .clk, .flush(s_flush), .start_w(w_start), .run(s_run), .req(w_req), .req_w(w_rw), .gnt(w_gnt),
        .rsp_v(app_rd_data_valid && rtag), .rsp_d(app_rd_data),
        .adv(w_adv), .head(w_head), .hv(w_hv), .busy(w_busy));

    // A command is presented (app_en) until app_rdy; a write's data goes with it
    // (app_wdf_wren until app_wdf_rdy).  Writes first: the ring must keep up.
    reg  rr = 0;                           // read turn: 0 prefix, 1 window
    reg  wd_pend = 0;                      // write data not yet accepted
    wire cmd_free = !app_en || app_rdy;    // no command waiting (or it goes now)
    wire do_w  = cmd_free && !wd_pend && w_v;
    wire do_rp = cmd_free && !do_w && p_req && (!w_req || !rr);
    wire do_rw = cmd_free && !do_w && w_req && !do_rp;
    assign w_pop = do_w;
    assign p_gnt = do_rp;
    assign w_gnt = do_rw;

    always_ff @(posedge clk) begin
        if (app_en && app_rdy) app_en <= 1'b0;
        if (app_wdf_wren && app_wdf_rdy) begin app_wdf_wren <= 1'b0; wd_pend <= 1'b0; end
        if (do_w) begin
            app_en   <= 1'b1;
            app_cmd  <= 3'd0;
            app_addr <= w_ring ? {1'b1, w_n[LGR+2:3], 3'b000}
                               : {1'b0, (LGR-LGP)'(0), w_pa, 3'b000};
            app_wdf_data <= w_d;
            app_wdf_wren <= 1'b1;
            wd_pend      <= 1'b1;
            if (w_ring) wr_upto <= w_n + 48'd8;
        end else if (do_rp || do_rw) begin
            app_en   <= 1'b1;
            app_cmd  <= 3'd1;
            app_addr <= do_rp ? {1'b0, p_rw, 3'b000} : {1'b1, w_rw, 3'b000};
            tag[tw]  <= do_rw;
            tw       <= tw + 1'b1;
            rr       <= do_rp;
        end
        if (app_rd_data_valid) tr <= tr + 1'b1;
        if (rst) begin app_en <= 1'b0; app_wdf_wren <= 1'b0; wd_pend <= 1'b0; wr_upto <= 0; end
    end

    // =========================================================================
    // The lane
    // =========================================================================
    typedef enum logic [3:0] {IDLE, LIM1, LIM2, LIM3, CLR, FILL, GVAL, SCAN, FIN, DRAIN} st_t;
    st_t st = IDLE;
    reg  [47:0] n = 0, m = 0, oldest = 0, lw = 0;
    reg  [31:0] amax = 0, a = 0, lh = 0, lp = 0;
    reg  [8:0]  g = 0, left = 0;
    reg  [3:0]  fc = 0;
    reg  [4:0]  ca = 0;
    reg  [2:0]  wc = 0;
    reg         busy = 0;                  // counting (left > 0)

    assign j_pop = st == IDLE && j_v && j_n < wr_upto && !rst;

    // pipeline of a pair (as check_group_lr)
    (* ram_style = "distributed" *) reg [7:0] seen [0:31];
    initial for (int i = 0; i < 32; i++) seen[i] = 8'd0;
    wire       stepping = st == SCAN && p_hv && w_hv;
    assign p_adv = stepping;
    assign w_adv = stepping || ((st == FILL || st == GVAL) && w_hv);
    reg  [7:0] x_q = 0;
    reg        cnt_q = 0, hit_q = 0;
    wire [7:0] word = seen[x_q[7:3]];
    wire       hit  = cnt_q && busy && !word[x_q[2:0]];
    always_ff @(posedge clk) begin
        x_q   <= 8'(p_head ^ w_head);
        cnt_q <= stepping && is_common(p_head) && (p_head ^ w_head) < g;
        hit_q <= hit;
        if (st == CLR)          seen[ca] <= 8'd0;
        else if (cnt_q && busy) seen[x_q[7:3]] <= word | (8'd1 << x_q[2:0]);
    end

    always_ff @(posedge clk) begin
        s_flush <= 1'b0;
        r_v     <= 1'b0;
        if (hit_q && busy) begin
            left <= left - 1'b1;
            if (left == 9'd1) busy <= 1'b0;
        end
        case (st)
            IDLE: if (j_pop) begin n <= j_n; m <= j_n - 1'b1; st <= LIM1; end
            // limits, over three clocks (48-bit arithmetic)
            LIM1: begin
                oldest <= (wr_upto > 48'(RINGV - MARGIN) &&
                           wr_upto - 48'(RINGV - MARGIN) > n_lo) ? wr_upto - 48'(RINGV - MARGIN) : n_lo;
                lh <= 32'(m >> 1);
                lp <= pre_len - 1;
                st <= LIM2;
            end
            LIM2: begin
                lw <= (m > oldest) ? m - oldest : 48'd0;
                st <= LIM3;
            end
            LIM3: begin
                amax <= (lw < 48'(lh)) ? ((32'(lw) < lp) ? 32'(lw) : lp)
                                       : ((lh < lp) ? lh : lp);
                // streams: prefix from word 0, window from the word holding G(n)
                w_start <= n[LGR+2:3];
                s_flush <= 1'b1;
                ca <= 0;
                st <= CLR;
            end
            CLR: begin                     // clear seen; the streams fill meanwhile
                s_run <= 1'b1;
                ca <= ca + 1'b1;
                if (ca == 5'd31) begin fc <= 4'(7 - n[2:0]); st <= (n[2:0] == 3'd7) ? GVAL : FILL; end
            end
            FILL: if (w_hv) begin          // skip the values above G(n) in its word
                fc <= fc - 1'b1;
                if (fc == 1) st <= GVAL;
            end
            GVAL: if (w_hv) begin          // G(n) itself: g and R(g)
                g    <= w_head;
                left <= RB[w_head];
                busy <= 1'b1;
                a    <= 0;
                st   <= SCAN;
            end
            SCAN: begin
                if (stepping) a <= a + 1'b1;
                if ((stepping && a == amax) || !busy) begin wc <= 0; st <= FIN; end
            end
            FIN: begin                     // let the pipeline drain, then report
                wc <= wc + 1'b1;
                if (wc == 3'd5) begin
                    r_v  <= 1'b1;
                    r_n  <= n;
                    r_ok <= !busy;
                    s_run <= 1'b0;
                    st   <= DRAIN;
                end
            end
            DRAIN: if (!p_busy && !w_busy && !app_en) begin   // nothing in flight: ready for the next
                busy <= 1'b0;
                st   <= IDLE;
            end
            default: st <= IDLE;
        endcase
        if (rst) begin st <= IDLE; s_run <= 1'b0; busy <= 1'b0; end
    end
endmodule
`default_nettype wire
