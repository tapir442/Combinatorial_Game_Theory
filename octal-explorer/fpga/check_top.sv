// =============================================================================
// check_top.sv -- the split check as the speculation core would see it:
// job builder + NC clusters (check_cluster.sv) + verified frontier + fail list
//
// STATUS: for resource and clock estimates (synth_check.tcl) and the functional
// test tb_check_top.sv.  Not yet connected to nexys_a7_top.
//
// -----------------------------------------------------------------------------
// Inputs from the core (the top level's g_new / g_comp / g_val)
// -----------------------------------------------------------------------------
// ev / comp / v: a value enters the history (once per shift, in order); comp =
// the core computed it.  `restart` (with n_set) says that the next ev is heap
// n_set: the first value of the load ('L').  The load must cover at least
// LIM + 32 values below the first computed heap (the board loads FOLD_DEPTH =
// 20,627).  Jobs, job numbers and the frontier start at the first computed heap.
//
//   * every value is written into the clusters' window rings at n mod KW;
//   * every computed value, with R(g) from the table RB, goes into the job
//     being assembled; after GS of them the job is pending, and goes to the
//     first cluster with an idle group (job_rdy);
//   * stall (registered) asks the core to stop: while a job is pending (the
//     next one could not be stored), or while the newest value is so far
//     ahead of the oldest job still running that the ring would overwrite
//     values it reads (n >= frontier + KW - LIM - 64).  The core may still
//     deliver a few values after stall rises; the job being assembled has
//     room for GS.
//
// -----------------------------------------------------------------------------
// Outputs
// -----------------------------------------------------------------------------
//   frontier  every heap n_set <= n < frontier has been checked: settled, or in
//             the fail list.  Jobs finish out of order; a ring of 64 done bits
//             (by job number) lets the frontier pass only finished jobs.
//   fail list FIFO of (n0, mask): mask bit l = heap n0 + l not settled within
//             LIM pairs.  These need the fallback (deeper scan).  f_pop takes
//             the head.
//   err       sticky: a job completed while one was pending, a fail entry was
//             lost, or the fail FIFO overflowed.  The run is then not verified.
//
// Precondition (check_group_lr): n_set >= 100,000.
// =============================================================================
`default_nettype none
module check_top #(
    parameter int GS  = 16,                // lanes per group (a power of 2)
    parameter int NS  = 8,                 // groups per cluster (<= 13)
    parameter int NC  = 2,                 // clusters
    parameter int KP  = 16384,
    parameter int KW  = 32768,
    parameter int LIM = 16384,
    parameter int FD  = 512                // fail FIFO depth
) (
    input  wire               clk,
    // ---- prefix load (once) ----
    input  wire               pw_en,
    input  wire [$clog2(KP)-1:0] pw_a,
    input  wire [8:0]         pw_v,
    // ---- from the core ----
    input  wire               restart,     // next ev is heap n_set
    input  wire [47:0]        n_set,
    input  wire               ev,
    input  wire               comp,
    input  wire [8:0]         v,
    output reg                stall = 0,
    // ---- results ----
    output reg  [47:0]        frontier = 0,
    output wire               f_v,         // fail list not empty
    output wire [47:0]        f_n0,
    output wire [GS-1:0]      f_mask,
    input  wire               f_pop,
    output reg  [31:0]        f_count = 0, // fail entries so far
    output reg                err = 0
);
    localparam int LG = $clog2(GS);

    import check_pkg::RB;                  // R(g) table

    // =========================================================================
    // Job builder
    // =========================================================================
    reg              ev_q = 0, comp_q = 0;
    reg  [8:0]       v_q = 0;
    reg  [47:0]      n = 0;                // heap of the next value
    reg  [47:0]      nbase = 0;            // first computed heap: job numbers count from here
    reg              started = 0;          // a computed value has come since restart
    reg  [LG-1:0]    cnt = 0;              // values in the job being assembled
    reg  [47:0]      acc_n0 = 0;
    reg  [GS*9-1:0]  acc_g = 0, acc_r = 0; // shift registers: lane GS-1 = newest
    reg              done_acc = 0;         // acc holds a complete job (one clock)
    reg              err_b = 0, err_r = 0; // sticky errors of the builder / the result side
    always_ff @(posedge clk) err <= err_b | err_r;
    reg              pend_v = 0;
    reg  [47:0]      pend_n0 = 0;
    reg  [GS*9-1:0]  pend_g = 0, pend_r = 0;

    wire [NC-1:0]    job_rdy;
    wire [NC-1:0]    job_v;
    for (genvar c = 0; c < NC; c++) begin : jsel
        if (c == 0) assign job_v[c] = pend_v && job_rdy[0];
        else        assign job_v[c] = pend_v && job_rdy[c] && !(|job_rdy[c-1:0]);
    end

    // ring writes, registered once more (they fan out to NC clusters)
    reg              ww_en = 0;
    reg  [$clog2(KW)-1:0] ww_a = 0;
    reg  [8:0]       ww_v = 0;

    always_ff @(posedge clk) begin
        ev_q   <= ev && !restart;
        comp_q <= comp;
        v_q    <= v;
        ww_en  <= ev_q;
        ww_a   <= n[$clog2(KW)-1:0];
        ww_v   <= v_q;
        done_acc <= 1'b0;
        if (restart) begin
            n       <= n_set;
            cnt     <= 0;
            pend_v  <= 1'b0;
            started <= 1'b0;
        end else begin
            if (ev_q) begin
                n <= n + 1'b1;
                if (comp_q && !started) begin
                    started <= 1'b1;
                    nbase   <= n;
                end
                if (comp_q) begin
                    acc_g <= {v_q, acc_g[GS*9-1:9]};
                    acc_r <= {RB[v_q], acc_r[GS*9-1:9]};
                    if (cnt == 0) acc_n0 <= n;
                    cnt <= cnt + 1'b1;                     // wraps to 0 after GS
                    if (cnt == LG'(GS - 1)) done_acc <= 1'b1;
                end
            end
            if (|job_v) pend_v <= 1'b0;
            if (done_acc) begin
                if (pend_v && !(|job_v)) err_b <= 1'b1;   // no room: a job is lost
                pend_v  <= 1'b1;
                pend_n0 <= acc_n0;
                pend_g  <= acc_g;
                pend_r  <= acc_r;
            end
        end
    end

    // =========================================================================
    // Clusters
    // =========================================================================
    wire [NC-1:0]    res_v;
    wire [47:0]      res_n0   [NC];
    wire [GS-1:0]    res_fail [NC];
    for (genvar c = 0; c < NC; c++) begin : cl
        check_cluster #(.GS(GS), .NS(NS), .KP(KP), .KW(KW), .LIM(LIM)) u (
            .clk, .pw_en, .pw_a, .pw_v, .ww_en, .ww_a, .ww_v,
            .job_v(job_v[c]), .job_rdy(job_rdy[c]), .job_n0(pend_n0), .job_g(pend_g), .job_r(pend_r),
            .res_v(res_v[c]), .res_n0(res_n0[c]), .res_fail(res_fail[c]));
    end

    // =========================================================================
    // Results: done bits and frontier; fail entries into the FIFO
    // =========================================================================
    reg  [63:0]      dmap = 0;             // job k finished (k mod 64)
    reg  [5:0]       front = 0;            // job number of the frontier (mod 64)
    reg  [NC-1:0]    rv_q = 0;
    reg  [5:0]       ridx [NC];
    reg  [NC-1:0]    skv = 0;              // per cluster: a fail entry waiting
    reg  [47:0]      skn [NC];
    reg  [GS-1:0]    skm [NC];

    // fail FIFO (block RAM)
    (* ram_style = "block" *) reg [47+GS:0] fmem [0:FD-1];
    reg  [$clog2(FD):0] fw = 0, fr = 0;    // write / read pointers, one extra bit
    reg              f_full = 0;           // registered, 2 entries early (one push per clock)
    reg  [47+GS:0]   f_head = 0;
    reg              f_hv = 0;
    assign f_v = f_hv;
    assign {f_n0, f_mask} = f_head;

    // which waiting entry goes into the FIFO this clock: the lowest cluster
    wire [NC-1:0]    skpick;
    for (genvar c = 0; c < NC; c++) begin : skp
        if (c == 0) assign skpick[c] = skv[0];
        else        assign skpick[c] = skv[c] && !(|skv[c-1:0]);
    end

    reg  [47:0]      lag = 0;              // n - frontier
    reg              far = 0;              // ... too large: the ring would overrun
    wire first_comp = ev_q && comp_q && !started && !restart;   // frontier starts here

    always_ff @(posedge clk) begin
        // stage 1: job number of each result (n0 - nbase) / GS, mod 64
        for (int c = 0; c < NC; c++) begin
            logic [47:0] d;
            d = res_n0[c] - nbase;
            rv_q[c] <= res_v[c];
            ridx[c] <= d[LG +: 6];
            if (res_v[c] && |res_fail[c]) begin
                if (skv[c] && !skpick[c]) err_r <= 1'b1;   // entry lost (never seen: fails are rare)
                skv[c] <= 1'b1;
                skn[c] <= res_n0[c];
                skm[c] <= res_fail[c];
            end else if (skpick[c] && !f_full) begin
                skv[c] <= 1'b0;
            end
        end
        // stage 2: mark done; move the frontier over finished jobs
        for (int c = 0; c < NC; c++) if (rv_q[c]) dmap[ridx[c]] <= 1'b1;
        if (dmap[front]) begin
            dmap[front] <= 1'b0;
            front    <= front + 1'b1;
            frontier <= frontier + 48'(GS);
        end
        // fail FIFO
        if (|skv) begin
            if (f_full) err_r <= 1'b1;
            else for (int c = 0; c < NC; c++) if (skpick[c]) begin
                fmem[fw[$clog2(FD)-1:0]] <= {skn[c], skm[c]};
                fw      <= fw + 1'b1;
                f_count <= f_count + 1'b1;
            end
        end
        f_full <= (fw - fr) >= ($clog2(FD)+1)'(FD - 2);
        if (!f_hv || f_pop) begin
            if (fr != fw) begin
                f_head <= fmem[fr[$clog2(FD)-1:0]];
                f_hv   <= 1'b1;
                fr     <= fr + 1'b1;
            end else f_hv <= 1'b0;
        end
        // stall: a job pending, or the ring would overrun the oldest running job
        // (the lag in two stages: subtract, then compare; the margin of 64 covers it)
        lag   <= n - frontier;
        far   <= started && lag >= 48'(KW - LIM - 64);
        stall <= pend_v || done_acc || far;
        if (first_comp) frontier <= n;
        if (restart) begin
            dmap  <= 0;
            front <= 0;
            stall <= 1'b0;
        end
    end
endmodule
`default_nettype wire
