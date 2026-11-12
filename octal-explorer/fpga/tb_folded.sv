// Testbench for officers_folded: fast preload (with gaps), two slow loads, then
// NRUN values with random stalls, compared with octal's values (preload.hex /
// expected.hex, written by sim_folded.sh / make_vectors.py).
// Inputs change at the falling edge.  ce_d / ld_fast_d are for the NEXT clock;
// ld_slow / ld_data are for the current clock (shifting tells whether it shifts).
`timescale 1ns/1ps
module tb_folded;
    parameter int PRELOAD = 20629;
    parameter int NRUN = 2000;
    parameter int STALL = 20;          // percent of clocks with ce = 0

    reg clk = 0, rst = 1, ce_d = 0, ld_fast_d = 0, ld_slow = 0;
    reg [8:0] ld_data = 0;
    wire shifting, out_valid, err;
    wire [8:0] out_data;
    officers_folded dut (.clk(clk), .rst(rst), .ce_d(ce_d), .ld_fast_d(ld_fast_d), .ld_slow(ld_slow),
                         .ld_data(ld_data), .shifting(shifting), .out_valid(out_valid),
                         .out_data(out_data), .err(err));
    always #1 clk = ~clk;

    reg [8:0] pre [0:PRELOAD-1];
    reg [8:0] exp_ [0:NRUN-1];
    int got = 0, bad = 0, i = 0, stalls = 0;
    bit pend = 0, consumed = 0;

    function automatic bit go();
        return $urandom_range(99) >= STALL;
    endfunction

    initial begin
        $readmemh("preload.hex", pre);
        $readmemh("expected.hex", exp_);
        repeat (3) @(negedge clk);
        rst = 0;
        // fast preload: ld_fast_d announces a shift in the next clock, which
        // takes the ld_data set at that clock's falling edge
        while (i < PRELOAD - 2 || pend) begin
            @(negedge clk);
            if (pend) begin ld_data = pre[i]; i++; end
            pend = (i < PRELOAD - 2) && ($urandom_range(99) >= 10);
            ld_fast_d = pend;
        end
        // the last two values go through full periods (ld_slow)
        @(negedge clk);
        ld_slow = 1;
        ld_data = pre[i];
        ce_d = go();
        while (i < PRELOAD) begin
            @(negedge clk);
            if (consumed) begin ld_data = pre[i < PRELOAD ? i : PRELOAD-1]; consumed = 0; end
            if (shifting) begin i++; consumed = 1; end
            ce_d = go();
        end
        // run
        while (got < NRUN) begin
            @(negedge clk);
            ld_slow = 0;
            ce_d = go();
            if (!ce_d) stalls++;
        end
        ce_d = 0;
        $display("RESULT %0d values, %0d mismatches, err=%0d (%0d stalled clocks)", got, bad, err, stalls);
        $finish;
    end

    always @(posedge clk) if (out_valid && got < NRUN) begin
        if (out_data !== exp_[got]) begin
            if (bad < 10) $display("MISMATCH #%0d: got %0d expected %0d", got, out_data, exp_[got]);
            bad++;
        end
        got++;
    end
endmodule
