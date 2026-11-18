// =============================================================================
// tb_check_group_lr.sv -- check_group_lr against the model's witness depths
//
// Run with sim_check_group_lr.sh (vectors from gen_check_vectors.py).
//
// A behavioural controller drives one lockstep group over the heaps of
// expect.txt, GS at a time, with real values of G:
//   1. wait until all lanes are ready (seen clear);
//   2. load every lane l with g = G(n0 + l) and R(g), in one clock;
//   3. three idle clocks (the load pipeline), then step a = 1, 2, ...: pre = G(a), win[j] =
//      G(m0 + GS-1 - a - j), m0 = n0 - 1;
//   4. for each lane note the a being presented when its `done` first rises;
//      offset = that a - the expected depth must be the same constant
//      (the pipeline latency, 3) for every heap;
//   5. stop stepping when all lanes are done.
// Prints one summary line: heaps, mismatches, clocks, and the offsets seen.
// =============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_check_group_lr;
    parameter int GS    = 4;
    parameter int NMAX  = 400000;          // size of the G array (the script sets N0 + HEAPS)
    parameter int HMAX  = 4096;            // most heaps read from expect.txt (the script sets HEAPS)

    reg clk = 0;
    always #2.5 clk = ~clk;

    reg                   start = 0, step = 0;
    reg  [GS*9-1:0]       ld_g = 0, ld_r = 0;
    reg  [8:0]            pre = 0;
    reg  [GS*9-1:0]       win = 0;
    wire [GS-1:0]         done, ready;

    check_group_lr #(.GS(GS)) dut (.clk, .start, .ld_g, .ld_r, .abort(1'b0), .step, .pre, .win, .done, .ready);

    reg [8:0] G [0:NMAX-1];

    // R(g): rare-class values (popcount(v & 0x1EE) even) below g
    function automatic int rare_below(int g);
        int r = 0;
        for (int v = 0; v < g; v++) if (!(^(9'(v) & 9'h1EE))) r++;
        return r;
    endfunction
    int en [0:HMAX-1], eg [0:HMAX-1], ed [0:HMAX-1];
    int nh = 0;

    initial begin
        int fd, k, r, bad, off, offmin, offmax, a, m0, n0, hits;
        longint clocks;
        bit seen_done [GS];
        string dir;
        if (!$value$plusargs("dir=%s", dir)) dir = ".";
        $readmemh({dir, "/g.hex"}, G);
        fd = $fopen({dir, "/expect.txt"}, "r");
        if (fd == 0) $fatal(1, "no expect.txt");
        while (nh < HMAX && $fscanf(fd, "%d %d %d", en[nh], eg[nh], ed[nh]) == 3) nh++;
        $fclose(fd);
        nh -= nh % GS;                     // whole groups only

        bad = 0; offmin = 1 << 30; offmax = -(1 << 30); clocks = 0; hits = 0;
        for (k = 0; k < nh; k += GS) begin
            n0 = en[k];
            m0 = n0 - 1;
            // 1. all lanes ready
            r = 0;
            while (ready != {GS{1'b1}}) begin
                @(posedge clk); clocks++;
                if (++r > 1000) begin
                    $display("ERROR: lanes not ready before n = %0d", n0);
                    $display("FAIL");
                    $finish;
                end
            end
            // 2. load
            for (int l = 0; l < GS; l++) begin
                if (G[n0 + l] != eg[k + l]) $fatal(1, "g.hex and expect.txt disagree at %0d", n0 + l);
                ld_g[l*9 +: 9] <= G[n0 + l];
                ld_r[l*9 +: 9] <= rare_below(G[n0 + l]);
            end
            start <= 1;
            @(posedge clk); clocks++;
            start <= 0;
            // 3. idle
            repeat (3) begin @(posedge clk); clocks++; end
            // 4./5. scan
            for (int l = 0; l < GS; l++) seen_done[l] = 0;
            a = 1;
            forever begin
                step <= 1;
                pre  <= G[a];
                for (int j = 0; j < GS; j++) win[j*9 +: 9] <= G[m0 + GS-1 - a - j];
                @(posedge clk); clocks++;
                for (int l = 0; l < GS; l++)
                    if (!seen_done[l] && done[l]) begin
                        seen_done[l] = 1;
                        off = a - ed[k + l];         // a = the step presented at this edge
                        if (off < offmin) offmin = off;
                        if (off > offmax) offmax = off;
                        hits++;
                    end
                if (seen_done.and()) break;
                a++;
                if (2 * a > m0 + 2 * GS + 16) begin
                    $display("ERROR: group at n = %0d not done at a = %0d", n0, a);
                    bad++;
                    break;
                end
            end
            step <= 0;
        end
        // the offset must be one constant for every heap
        if (offmin != offmax) bad++;
        $display("heaps %0d from n = %0d, lanes done %0d, clocks %0d, offset min %0d max %0d, errors %0d",
                 nh, en[0], hits, clocks, offmin, offmax, bad);
        if (bad == 0) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
`default_nettype wire
