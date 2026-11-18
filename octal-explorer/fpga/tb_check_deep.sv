// =============================================================================
// tb_check_deep.sv -- check_deep against the model, with a MIG native model
//
// Run with sim_check_deep.sh (vectors from gen_check_vectors.py).
//
// MIG model: app_rdy and app_wdf_rdy low at random (RDY_PCT % of the clocks
// high); reads return in order, 20-40 clocks after the command.
//
//   1. write the prefix G(0 .. PRE-1) and the ring G(NLO .. end), NLO a
//      multiple of 8; pre_len = PRE, n_lo = NLO;
//   2. jobs: every heap of expect.txt with depth > DEEP, and every STRIDE-th
//      heap;
//   3. each result: r_ok must be (depth <= amax), amax = min(m/2, PRE-1,
//      m - NLO) (the ring here never wraps).
// =============================================================================
`timescale 1ns/1ps
`default_nettype none
module tb_check_deep;
    parameter int PRE     = 65536;
    parameter int NLO     = 900000;
    parameter int DEEP    = 8000;
    parameter int STRIDE  = 64;
    parameter int RDY_PCT = 70;
    parameter int NMAX    = 1100000;
    parameter int HMAX    = 4096;

    reg clk = 0;
    always #6 clk = ~clk;                  // ~83 MHz

    reg rst = 1;
    reg  w_v = 0, w_ring = 0, j_v = 0;
    reg  [47:0] w_n = 0, j_n = 0;
    reg  [20:0] w_pa = 0;
    reg  [127:0] w_d = 0;
    wire w_pop, j_pop, r_v, r_ok;
    wire [47:0] r_n, wr_upto;
    wire [26:0] app_addr;
    wire [2:0]  app_cmd;
    wire        app_en, app_wdf_wren, app_wdf_end;
    wire [127:0] app_wdf_data;
    wire [15:0] app_wdf_mask;
    reg         app_rdy = 0, app_wdf_rdy = 0, app_rd_data_valid = 0;
    reg  [127:0] app_rd_data = 0;

    check_deep dut (.clk, .rst, .w_v, .w_ring, .w_n, .w_pa, .w_d, .w_pop,
        .n_lo(48'(NLO)), .pre_len(32'(PRE)), .j_v, .j_n, .j_pop, .r_v, .r_n, .r_ok, .wr_upto,
        .app_addr, .app_cmd, .app_en, .app_rdy, .app_wdf_data, .app_wdf_wren, .app_wdf_end,
        .app_wdf_mask, .app_wdf_rdy, .app_rd_data, .app_rd_data_valid);

    // ---- MIG model ----
    reg [127:0] mem [longint];
    longint now = 0;
    longint q_t [$];                       // read queue: return time and word
    longint q_a [$];
    longint last_ret = 0;
    longint n_rd = 0, n_wr = 0;
    always @(posedge clk) begin
        now++;
        // accept a command / write data presented in this clock (if ready)
        if (app_en && app_rdy) begin
            if (app_cmd == 3'd1) begin
                longint t = now + 20 + $urandom_range(0, 20);
                if (t <= last_ret) t = last_ret + 1;
                last_ret = t;
                q_t.push_back(t);
                q_a.push_back(longint'(app_addr >> 3));
                n_rd++;
            end
        end
        if (app_wdf_wren && app_wdf_rdy) begin
            // command and data come together in check_deep (address held while app_en)
            mem[longint'(app_addr >> 3)] = app_wdf_data;
            n_wr++;
        end
        app_rd_data_valid <= 0;
        if (q_t.size() && q_t[0] <= now) begin
            longint a = q_a.pop_front();
            void'(q_t.pop_front());
            app_rd_data_valid <= 1;
            app_rd_data <= mem.exists(a) ? mem[a] : {8{16'h01ff}};   // garbage if never written
        end
        app_rdy     <= ($urandom_range(0, 99) < RDY_PCT);
        app_wdf_rdy <= ($urandom_range(0, 99) < RDY_PCT);
    end

    reg [8:0] G [0:NMAX-1];
    int en [0:HMAX-1], eg [0:HMAX-1], ed [0:HMAX-1];
    int nh = 0;

    function automatic [127:0] pack8(int base);
        logic [127:0] w = 0;
        for (int i = 0; i < 8; i++) w[i*16 +: 16] = 16'(G[base + i]);
        return w;
    endfunction

    int jobs [$];
    int bad = 0, nres = 0, nok = 0, nunres = 0;

    // results
    always @(posedge clk) if (r_v) begin
        automatic int k = int'(r_n) - en[0];
        automatic longint m = longint'(r_n) - 1;
        automatic longint amax = m / 2;
        automatic bit exp_ok;
        if (PRE - 1 < amax) amax = PRE - 1;
        if (m - NLO < amax) amax = m - NLO;
        exp_ok = ed[k] <= amax;
        nres++; nok += r_ok; nunres += !r_ok;
        if (r_ok != exp_ok) begin
            if (bad < 10) $display("ERROR: heap %0d depth %0d amax %0d: ok %0d", r_n, ed[k], amax, r_ok);
            bad++;
        end
    end

    initial begin
        int fd, nw;
        string dir;
        if (!$value$plusargs("dir=%s", dir)) dir = ".";
        $readmemh({dir, "/g.hex"}, G);
        fd = $fopen({dir, "/expect.txt"}, "r");
        if (fd == 0) $fatal(1, "no expect.txt");
        while (nh < HMAX && $fscanf(fd, "%d %d %d", en[nh], eg[nh], ed[nh]) == 3) nh++;
        $fclose(fd);
        for (int k = 0; k < nh; k++) if (ed[k] > DEEP || k % STRIDE == 0) jobs.push_back(k);
        if (NLO % 8 != 0) $fatal(1, "NLO must be a multiple of 8");

        repeat (5) @(negedge clk);
        rst = 0;
        // 1. prefix and ring words (through w_*, which waits for w_pop)
        for (int wa = 0; wa < PRE / 8; wa++) begin
            w_v = 1; w_ring = 0; w_pa = wa; w_d = pack8(8 * wa);
            do @(negedge clk); while (!w_pop_seen());
        end
        for (int h = NLO; h + 8 <= en[nh-1] + 8; h += 8) begin
            w_v = 1; w_ring = 1; w_n = h; w_d = pack8(h);
            do @(negedge clk); while (!w_pop_seen());
        end
        w_v = 0;
        // 2. jobs
        foreach (jobs[i]) begin
            j_v = 1; j_n = en[jobs[i]];
            do @(negedge clk); while (!j_pop_seen());
        end
        j_v = 0;
        for (int t = 0; t < 4_000_000 && nres < jobs.size(); t++) @(negedge clk);
        if (nres != jobs.size()) begin $display("ERROR: %0d of %0d results", nres, jobs.size()); bad++; end
        $display("deep: %0d heaps (PRE %0d, NLO %0d, ready %0d %%): verified %0d, unresolved %0d, %0d reads, %0d writes, %0d clocks, errors %0d",
                 jobs.size(), PRE, NLO, RDY_PCT, nok, nunres, n_rd, n_wr, now, bad);
        if (bad == 0) $display("PASS"); else $display("FAIL");
        $finish;
    end

    // w_pop / j_pop as they were at the last rising edge
    bit wp_s = 0, jp_s = 0;
    always @(posedge clk) begin wp_s <= w_pop; jp_s <= j_pop; end
    function automatic bit w_pop_seen(); return wp_s; endfunction
    function automatic bit j_pop_seen(); return jp_s; endfunction
endmodule
`default_nettype wire
