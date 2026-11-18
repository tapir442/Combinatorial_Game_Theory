// =============================================================================
// tb_check_cluster.sv -- check_cluster against the model's witness depths
//
// Run with sim_check_cluster.sh (vectors from gen_check_vectors.py).
//
//   1. write the prefix G(0 .. KP-1);
//   2. write the window ring up to the first job;
//   3. each clock: if the ring is not yet written up to the next job's last
//      heap, write one more value (like the speculation core, but faster);
//      otherwise offer the next job (GS heaps with g and R(g));
//   4. collect the results: lane l of the job at n0 must fail exactly when the
//      witness depth of heap n0 + l is >= LIM.
// The ring is written at most ~GS ahead of the newest job, and a job reads at
// most LIM + 16 values below its heaps; KW > LIM + NS*GS + 32 keeps them apart.
// Prints heaps, failed heaps (expected and seen), clocks per heap, errors.
// =============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_check_cluster;
    parameter int GS   = 16;
    parameter int NS   = 8;
    parameter int KP   = 16384;
    parameter int KW   = 32768;
    parameter int LIM  = 4096;
    parameter int NMAX = 400000;
    parameter int HMAX = 4096;

    reg clk = 0;
    always #2.5 clk = ~clk;

    reg                     pw_en = 0, ww_en = 0, job_v = 0;
    reg  [$clog2(KP)-1:0]   pw_a = 0;
    reg  [$clog2(KW)-1:0]   ww_a = 0;
    reg  [8:0]              pw_v = 0, ww_v = 0;
    reg  [47:0]             job_n0 = 0;
    reg  [GS*9-1:0]         job_g = 0, job_r = 0;
    wire                    job_rdy, res_v;
    wire [47:0]             res_n0;
    wire [GS-1:0]           res_fail;

    check_cluster #(.GS(GS), .NS(NS), .KP(KP), .KW(KW), .LIM(LIM)) dut (
        .clk, .pw_en, .pw_a, .pw_v, .ww_en, .ww_a, .ww_v,
        .job_v, .job_rdy, .job_n0, .job_g, .job_r, .res_v, .res_n0, .res_fail);

    reg [8:0] G [0:NMAX-1];
    int en [0:HMAX-1], eg [0:HMAX-1], ed [0:HMAX-1];
    int nh = 0;

    function automatic int rare_below(int g);
        int r = 0;
        for (int v = 0; v < g; v++) if (!(^(9'(v) & 9'h1EE))) r++;
        return r;
    endfunction

    int  bad = 0, results = 0, fail_exp = 0, fail_seen = 0;
    longint clocks = 0;
    bit  got [0:HMAX-1];

    // ---- results ----
    always @(posedge clk) begin
        clocks++;
        if (res_v) begin
            int k = int'(res_n0) - en[0];
            if (k < 0 || k >= nh || k % GS != 0 || got[k]) begin
                $display("ERROR: unexpected result n0 = %0d", res_n0);
                bad++;
            end else begin
                got[k] = 1;
                results++;
                for (int l = 0; l < GS; l++) begin
                    bit exp_f = ed[k + l] >= LIM;
                    fail_exp  += exp_f;
                    fail_seen += res_fail[l];
                    if (res_fail[l] != exp_f) begin
                        if (bad < 10) $display("ERROR: heap %0d depth %0d: fail %0d, expected %0d",
                                               en[k + l], ed[k + l], res_fail[l], exp_f);
                        bad++;
                    end
                end
            end
        end
    end

    // ---- stimulus ----
    initial begin
        int fd, wn, k;
        string dir;
        if (!$value$plusargs("dir=%s", dir)) dir = ".";
        $readmemh({dir, "/g.hex"}, G);
        fd = $fopen({dir, "/expect.txt"}, "r");
        if (fd == 0) $fatal(1, "no expect.txt");
        while (nh < HMAX && $fscanf(fd, "%d %d %d", en[nh], eg[nh], ed[nh]) == 3) nh++;
        $fclose(fd);
        nh -= nh % GS;
        for (int i = 0; i < nh; i++) got[i] = 0;
        if (KW <= LIM + NS * GS + 32) $fatal(1, "KW too small for LIM and the jobs in flight");

        // 1. prefix (inputs change on the falling edge, sampled on the rising one)
        for (int a = 0; a < KP; a++) begin
            @(negedge clk);
            pw_en = 1; pw_a = a; pw_v = G[a];
        end
        @(negedge clk);
        pw_en = 0;
        // 2./3. window and jobs; job_rdy is stable at the falling edge
        wn = en[0] - LIM - 64;             // next value to write into the ring
        k = 0;                             // next job (heap index)
        clocks = 0;
        while (k < nh) begin
            @(negedge clk);
            ww_en = 0; job_v = 0;
            if (wn < en[k] + GS) begin
                ww_en = 1; ww_a = wn % KW; ww_v = G[wn];
                wn++;
            end else if (job_rdy) begin
                job_v = 1; job_n0 = en[k];
                for (int l = 0; l < GS; l++) begin
                    job_g[l*9 +: 9] = G[en[k] + l];
                    job_r[l*9 +: 9] = rare_below(G[en[k] + l]);
                end
                k += GS;                   // taken at the next rising edge
            end
        end
        @(negedge clk);
        ww_en = 0; job_v = 0;
        // 4. wait for the last results
        for (int t = 0; t < 2 * LIM + 10000 && results < nh / GS; t++) @(posedge clk);
        if (results != nh / GS) begin
            $display("ERROR: %0d of %0d results", results, nh / GS);
            bad++;
        end
        $display("heaps %0d from n = %0d (GS %0d, NS %0d, LIM %0d): failed %0d (expected %0d), %0d clocks, %.1f clocks/heap, errors %0d",
                 nh, en[0], GS, NS, LIM, fail_seen, fail_exp, clocks, real'(clocks) / nh, bad);
        if (bad == 0) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
`default_nettype wire
