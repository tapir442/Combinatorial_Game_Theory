// 8N1 UART receiver / transmitter and a small byte FIFO.
`default_nettype none

module uart_rx #(parameter int DIV = 75) (     // clocks per bit
    input  wire       clk,
    input  wire       rst,
    input  wire       rx,
    output reg        valid,                   // one clock
    output reg  [7:0] data
);
    reg [2:0] sync = 3'b111;
    always @(posedge clk) sync <= {sync[1:0], rx};
    wire r = sync[2];
    reg busy = 0;
    reg [3:0] bitn;
    reg [$clog2(DIV*3/2+1)-1:0] cnt;
    always @(posedge clk) begin
        valid <= 1'b0;
        if (rst) busy <= 1'b0;
        else if (!busy) begin
            if (!r) begin busy <= 1'b1; cnt <= DIV + DIV/2 - 1; bitn <= 0; end   // middle of bit 0
        end else if (cnt != 0) cnt <= cnt - 1;
        else if (bitn < 8) begin
            data <= {r, data[7:1]};
            bitn <= bitn + 1;
            cnt <= DIV - 1;
        end else begin                          // stop bit
            busy <= 1'b0;
            valid <= r;
        end
    end
endmodule

module uart_tx #(parameter int DIV = 75) (
    input  wire       clk,
    input  wire       rst,
    input  wire       start,                   // with data, when !busy
    input  wire [7:0] data,
    output wire       busy,
    output reg        tx = 1'b1
);
    reg [9:0] sh = 0;
    reg [3:0] left = 0;
    reg [$clog2(DIV)-1:0] cnt = 0;
    assign busy = (left != 0) || (cnt != 0);   // includes the whole stop bit
    always @(posedge clk) begin
        if (rst) begin left <= 0; cnt <= 0; tx <= 1'b1; end
        else if (cnt != 0) cnt <= cnt - 1;
        else if (left != 0) begin
            tx <= sh[0];
            sh <= {1'b1, sh[9:1]};
            left <= left - 1;
            cnt <= DIV - 1;
        end else if (start) begin sh <= {1'b1, data, 1'b0}; left <= 10; end
    end
endmodule

module byte_fifo #(parameter int LOG = 10) (
    input  wire       clk,
    input  wire       rst,
    input  wire       wr,
    input  wire [7:0] wdata,
    input  wire       rd,                      // pop (when !empty)
    output wire [7:0] rdata,                   // head, valid when !empty
    output wire       empty,
    output wire [LOG:0] count
);
    reg [7:0] mem [0:(1<<LOG)-1];
    reg [LOG:0] wp = 0, rp = 0;
    assign count = wp - rp;
    assign empty = wp == rp;
    assign rdata = mem[rp[LOG-1:0]];
    always @(posedge clk) begin
        if (rst) begin wp <= 0; rp <= 0; end
        else begin
            if (wr) begin mem[wp[LOG-1:0]] <= wdata; wp <= wp + 1; end
            if (rd && !empty) rp <= rp + 1;
        end
    end
endmodule
`default_nettype wire
