// Testbench of window_fp: random values with random gaps (and runs of
// back-to-back events), each event's value and the resulting fingerprint are
// written to window_fp.log as "v fp" (hex).  check_window_fp.py recomputes the
// fingerprint of the window from scratch at sample points and compares.
//   ./sim_window_fp.sh
`timescale 1ns/1ps
`default_nettype none
module tb_window_fp;
    localparam int DEPTH = 20627;
    localparam int NEV = 3 * DEPTH + 1234;          // more than three windows
    reg clk = 0;
    always #2.5 clk = ~clk;
    reg ev = 0, comp = 0;
    reg [8:0] v = 0;
    wire [63:0] fp;
    wire upd, upd_comp;
    window_fp #(.DEPTH(DEPTH)) dut (.clk(clk), .ev(ev), .comp(comp), .v(v), .fp(fp),
                                     .upd(upd), .upd_comp(upd_comp));
    integer f, sent = 0, got = 0, gap, seed = 20261118;
    function automatic int unsigned rnd(); return $unsigned($random(seed)); endfunction
    reg [8:0] fifo [0:15];                          // values in flight (at most 2)
    initial begin
        f = $fopen("window_fp.log", "w");

        while (sent < NEV) begin
            @(negedge clk);
            // ~1/3 of the time a burst of back-to-back events, else gaps of 0..3 clocks
            gap = rnd() % 3 == 0 ? 0 : rnd() % 4;
            if (gap == 0) begin
                ev = 1; comp = rnd() % 2; v = rnd() % 512;
                fifo[sent % 16] = v; sent++;
            end else begin
                ev = 0;
                repeat (gap - 1) @(negedge clk);
            end
        end
        @(negedge clk) ev = 0;
        repeat (5) @(negedge clk);
        $display("sent %0d, logged %0d", sent, got);
        $fclose(f);
        $finish;
    end
    // fp and upd are written on the same edge: while upd is high, fp is the new value
    always @(posedge clk) begin
        if (upd) begin
            $fwrite(f, "%03x %016x\n", fifo[got % 16], fp);
            got++;
        end
    end
endmodule
`default_nettype wire
