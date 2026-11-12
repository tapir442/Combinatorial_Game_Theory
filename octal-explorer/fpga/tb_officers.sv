// Testbench for officers_core: preload G(n0 - PRELOAD .. n0-1), run NRUN
// values with random stalls (and random gaps in the preload), and compare
// every output with the reference values from octal (fpga/sim.sh writes the
// two hex files).
`timescale 1ns/1ps
module tb_officers;
    parameter int PRELOAD = 20629;
    parameter int NRUN = 10000;
    parameter int STALL_PCT = 20;    // percent of cycles with run = 0

    reg clk = 0, rst = 1, load = 0, run = 0;
    reg [8:0] load_data = 0;
    wire out_valid, err;
    wire [8:0] out_data;
    officers_core dut (.clk(clk), .rst(rst), .load(load), .load_data(load_data), .run(run),
                       .out_valid(out_valid), .out_data(out_data), .err(err));
    always #1 clk = ~clk;

    reg [8:0] pre [0:PRELOAD-1];
    reg [8:0] exp_ [0:NRUN-1];
    int got = 0, bad = 0, cycles = 0;

    initial begin
        $readmemh("preload.hex", pre);
        $readmemh("expected.hex", exp_);
        repeat (4) @(posedge clk);
        rst <= 0;
        for (int i = 0; i < PRELOAD; ) begin
            @(posedge clk);
            if ($urandom_range(99) < 10) begin load <= 0; end       // gap in the preload
            else begin load <= 1; load_data <= pre[i]; i++; end
        end
        @(posedge clk) load <= 0;
        while (got < NRUN) begin
            @(posedge clk);
            run <= ($urandom_range(99) >= STALL_PCT);
            cycles++;
        end
        run <= 0;
        @(posedge clk);
        $display("RESULT %0d values, %0d mismatches, err=%0d, %0d cycles", got, bad, err, cycles);
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
