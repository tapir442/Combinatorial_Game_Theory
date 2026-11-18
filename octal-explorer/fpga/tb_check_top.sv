// =============================================================================
// tb_check_top.sv -- check_top fed like the speculation core, against the model
//
// Run with sim_check_top.sh (vectors from gen_check_vectors.py).
//
//   1. prefix G(0 .. KP-1);
//   2. restart at n_set = N0 - PRE, then the load: G(N0-PRE .. N0-1), one per
//      clock, comp = 0 (PRE = 20,627, as the board loads);
//   3. the "core": G(N0), G(N0+1), ... with comp = 1, one value every PCORE
//      clocks, but not while stall is high (sampled STL clocks late, to model
//      the core's pipeline: values already under way still arrive);
//   4. pop the fail list as it fills; at the end the frontier must be N0 +
//      HEAPS, the fail list must be exactly the heaps of depth >= LIM, and err
//      must be 0.
// Prints the stall share and clocks per heap (PCORE = no stall at all).
// =============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_check_top;
    parameter int GS    = 16;
    parameter int NS    = 4;
    parameter int NC    = 2;
    parameter int KP    = 16384;
    parameter int KW    = 32768;
    parameter int LIM   = 4096;
    parameter int PCORE = 16;
    parameter int STL   = 3;
    parameter int PRE   = 20627;
    parameter int NMAX  = 400000;
    parameter int HMAX  = 4096;

    reg clk = 0;
    always #2.5 clk = ~clk;

    reg                    pw_en = 0, restart = 0, ev = 0, comp = 0, f_pop = 0;
    reg  [$clog2(KP)-1:0]  pw_a = 0;
    reg  [8:0]             pw_v = 0, v = 0;
    reg  [47:0]            n_set = 0;
    wire                   stall, f_v, err;
    wire [47:0]            frontier, f_n0;
    wire [GS-1:0]          f_mask;
    wire [31:0]            f_count;

    check_top #(.GS(GS), .NS(NS), .NC(NC), .KP(KP), .KW(KW), .LIM(LIM)) dut (
        .clk, .pw_en, .pw_a, .pw_v, .restart, .n_set, .ev, .comp, .v, .stall,
        .frontier, .f_v, .f_n0, .f_mask, .f_pop, .f_count, .err);

    reg [8:0] G [0:NMAX-1];
    int en [0:HMAX-1], eg [0:HMAX-1], ed [0:HMAX-1];
    int nh = 0;
    bit flagged [0:HMAX-1];
    int bad = 0, fails_seen = 0;

    // ---- fail list: pop and record ----
    always @(negedge clk) begin
        f_pop = 0;
        if (f_v) begin
            for (int l = 0; l < GS; l++) if (f_mask[l]) begin
                automatic int k = int'(f_n0) + l - en[0];
                if (k < 0 || k >= nh || flagged[k]) begin
                    $display("ERROR: bad fail entry heap %0d", int'(f_n0) + l); bad++;
                end else begin flagged[k] = 1; fails_seen++; end
            end
            f_pop = 1;
        end
    end

    initial begin
        int fd, sent, wait_c, fail_exp;
        longint clocks, stalled;
        bit stall_d [STL];
        string dir;
        if (!$value$plusargs("dir=%s", dir)) dir = ".";
        $readmemh({dir, "/g.hex"}, G);
        fd = $fopen({dir, "/expect.txt"}, "r");
        if (fd == 0) $fatal(1, "no expect.txt");
        while (nh < HMAX && $fscanf(fd, "%d %d %d", en[nh], eg[nh], ed[nh]) == 3) nh++;
        $fclose(fd);
        nh -= nh % GS;
        for (int i = 0; i < nh; i++) flagged[i] = 0;

        // 1. prefix
        for (int a = 0; a < KP; a++) begin
            @(negedge clk); pw_en = 1; pw_a = a; pw_v = G[a];
        end
        @(negedge clk); pw_en = 0;
        // 2. restart and load
        n_set = en[0] - PRE; restart = 1;
        @(negedge clk); restart = 0;
        for (int i = en[0] - PRE; i < en[0]; i++) begin
            ev = 1; comp = 0; v = G[i];
            @(negedge clk);
        end
        ev = 0;
        // 3. the core
        sent = 0; wait_c = 0; clocks = 0; stalled = 0;
        for (int i = 0; i < STL; i++) stall_d[i] = 0;
        while (sent < nh || frontier < en[0] + nh) begin
            ev = 0; comp = 0;
            if (sent < nh) begin
                if (stall_d[STL-1]) stalled++;
                else if (++wait_c >= PCORE) begin
                    ev = 1; comp = 1; v = G[en[0] + sent];
                    sent++; wait_c = 0;
                end
            end
            for (int i = STL-1; i > 0; i--) stall_d[i] = stall_d[i-1];
            stall_d[0] = stall;
            @(negedge clk);
            clocks++;
            if (clocks > 64'd200_000_000) begin $display("ERROR: timeout, frontier %0d", frontier); bad++; break; end
        end
        ev = 0;
        repeat (10) @(negedge clk);
        fail_exp = 0;
        for (int k = 0; k < nh; k++) begin
            automatic bit e = ed[k] >= LIM;
            fail_exp += e;
            if (e != flagged[k]) begin
                if (bad < 10) $display("ERROR: heap %0d depth %0d flagged %0d", en[k], ed[k], flagged[k]);
                bad++;
            end
        end
        if (err) begin $display("ERROR: err set"); bad++; end
        if (frontier != en[0] + nh) begin $display("ERROR: frontier %0d", frontier); bad++; end
        $display("heaps %0d from n = %0d (GS %0d, NS %0d, NC %0d, LIM %0d, PCORE %0d): flagged %0d (expected %0d), f_count %0d, %0d clocks, %.2f clocks/heap, stalled %.1f %%, errors %0d",
                 nh, en[0], GS, NS, NC, LIM, PCORE, fails_seen, fail_exp, f_count, clocks,
                 real'(clocks) / nh, 100.0 * stalled / clocks, bad);
        if (bad == 0) $display("PASS"); else $display("FAIL");
        $finish;
    end
endmodule
`default_nettype wire
