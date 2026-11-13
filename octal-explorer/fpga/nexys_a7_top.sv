// Nexys A7-100T top level for the folded Officers core.
//
// USB-UART (2 Mbaud, 8N1), little-endian.  Commands from the host:
//   'I'                      -> 'I' fold:u8 mhz:u16 depth:u32 slots:u16
//   'L' n:u32 n x u16        load G(n0-n .. n0-1); the last two go through full
//                            periods (ld_slow) so the pipeline is primed  -> 'l'
//   'S' n:u32                compute n values and stream each as u16       -> ... 's' err:u8
//   'C' n:u32                compute n values at full speed                -> 'c' crc:u32
//                            cycles:u64 last:u16 err:u8 max:u16 at:u32
//                            (crc = CRC-32 (zlib) of the values as u16 LE; max = the
//                            block's highest value, first at the at-th value, from 0)
//   'D'                      dump the history: DEPTH x u16, G(n-DEPTH .. n-1) for the
//                            next n, oldest first, then 'd'.  The history is rotated
//                            once around, so S and C continue afterwards; 'L' with two
//                            more values in front (any) and these restores the state.
// S, C and D continue from where the previous command stopped.
// LEDs: 0 busy, 1 err, 2 MMCM locked, 15..8 progress (top bits of the count).
// 7-segment display: the highest nim-value so far (preloaded or computed), in decimal;
// cleared by each 'L'.
`include "folded_info.svh"   // FOLD_P, FOLD_S, FOLD_DEPTH (gen_folded.py)
`default_nettype none
module nexys_a7_top #(
    parameter int MULT = 10,         // VCO = 100 MHz * MULT (600 .. 1200 MHz)
    parameter int DIVO = 5,          // core clock = VCO / DIVO  (200 MHz)
    parameter int BAUD = 2_000_000
) (
    input  wire        CLK100MHZ,
    input  wire        CPU_RESETN,
    input  wire        UART_TXD_IN,      // from the PC
    output wire        UART_RXD_OUT,     // to the PC
    output wire [15:0] LED,
    output wire [6:0]  SEG,              // CA..CG, active low
    output wire        DP,
    output wire [7:0]  AN                // digit enables, active low; AN[0] rightmost
);
    localparam int MHZ = 100 * MULT / DIVO;
    localparam int DIV = MHZ * 1_000_000 / BAUD;

    // ---- clock ----
    wire clkfb, clk_mmcm, locked, clk;
    MMCME2_BASE #(.CLKIN1_PERIOD(10.0), .CLKFBOUT_MULT_F(MULT), .CLKOUT0_DIVIDE_F(DIVO))
        u_mmcm (.CLKIN1(CLK100MHZ), .CLKFBIN(clkfb), .CLKFBOUT(clkfb), .CLKOUT0(clk_mmcm),
                .LOCKED(locked), .PWRDWN(1'b0), .RST(1'b0),
                .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
                .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB());
    BUFG u_bufg (.I(clk_mmcm), .O(clk));
    reg [3:0] rst_sh = 4'hF;
    always @(posedge clk) rst_sh <= {rst_sh[2:0], ~(locked & CPU_RESETN)};
    wire rst = rst_sh[3];

    // ---- UART ----
    wire       rx_v;
    wire [7:0] rx_d;
    uart_rx #(.DIV(DIV)) u_rx (.clk(clk), .rst(rst), .rx(UART_TXD_IN), .valid(rx_v), .data(rx_d));
    reg        q_wr;
    reg  [7:0] q_d;
    wire [7:0] f_d;
    wire       f_empty, tx_busy;
    wire [8:0] f_cnt;
    wire       f_pop = !f_empty && !tx_busy;
    byte_fifo #(.LOG(8)) u_fifo   (.clk(clk), .rst(rst), .wr(q_wr), .wdata(q_d), .rd(f_pop),
                                  .rdata(f_d), .empty(f_empty), .count(f_cnt));
    uart_tx #(.DIV(DIV)) u_tx (.clk(clk), .rst(rst), .start(f_pop), .data(f_d), .busy(tx_busy),
                               .tx(UART_RXD_OUT));

    // ---- core ----
    // ce_d / ld_fast_d: what the core does in the NEXT clock (it registers them, so
    // no combinational path from this FSM fans out into the core)
    reg        ce_d, ld_fast_d, ld_slow, ld_fast_q;
    reg  [8:0] ld_data;
    wire       shifting, out_valid, err;
    wire [8:0] out_data, tail;
    officers_folded u_core (.clk(clk), .rst(rst), .ce_d(ce_d), .ld_fast_d(ld_fast_d),
                            .ld_slow(ld_slow), .ld_data(ld_data), .shifting(shifting),
                            .out_valid(out_valid), .out_data(out_data), .err(err),
                            .tail(tail));
    // the oldest history entry as a value: bit 8 from the common flag (odd parity = common)
    wire [8:0] tail_v = {tail[8] ^ ^(tail[7:0] & 8'hEE), tail[7:0]};

    // ---- CRC-32 (reflected, poly EDB88320) of two bytes ----
    function automatic [31:0] crc16(input [31:0] c, input [15:0] d);
        c = c ^ {16'd0, d};
        for (int k = 0; k < 16; k++) c = c[0] ? (c >> 1) ^ 32'hEDB88320 : (c >> 1);
        return c;
    endfunction

    // ---- command FSM ----
    typedef enum logic [3:0] {IDLE, ARG, LOAD_LO, LOAD_HI, LOAD_PUT, RUN, REPLY, D_ROT, D_LO, D_HI} st_t;
    st_t st;
    reg [7:0]  cmd;
    reg [31:0] n, left, done;
    reg [1:0]  argi;
    reg [7:0]  lo;
    reg [31:0] crc;
    reg [31:0] cyc_lo, cyc_hi;          // cycle counter, carry into the high half one clock later
    reg        cyc_c;
    // flags of `left`, updated together with it, so that no 32-bit compare sits in front
    // of ce_d (which feeds the core's replicated shift enables)
    reg        l_gt2, l_gt1, l_eq1, l_nz;
    reg        room_q, rep_room_q;      // FIFO space, registered (the FIFO has ample margin)
    reg        all_out;                 // done == n, registered
    reg [8:0]  last;
    // block maximum: from registered copies of the output (off the critical path)
    reg        bv_q;
    reg [8:0]  bval_q, bmax;
    reg [31:0] bcnt, bmax_at;
    reg [7:0]  rep [0:21];
    reg [4:0]  rep_n, rep_i;

    reg hi_pending = 0, hi_bit;          // high byte of a streamed value, one clock later
    always @(posedge clk) begin
        hi_pending <= (st == RUN && cmd == "S" && out_valid);
        hi_bit <= out_data[8];
    end

    // the core runs in RUN while values are left and there is room for its output;
    // in LOAD_PUT until the slow-loaded value has been shifted in
    wire [31:0] arg = {rx_d, n[31:8]};
    always @(*) begin
        ce_d = 1'b0;
        ld_fast_d = 1'b0;
        ld_slow = (st == LOAD_PUT);
        case (st)
        LOAD_HI:  begin ld_fast_d = rx_v && l_gt2; ce_d = rx_v && !l_gt2; end
        LOAD_PUT: ce_d = !shifting;
        RUN:      ce_d = room_q && (l_gt1 || (l_eq1 && !shifting));
        D_ROT:    ld_fast_d = l_nz && rep_room_q;      // shift the oldest value back in
        default:  ;
        endcase
    end

    always @(posedge clk) begin
        q_wr <= 1'b0;
        ld_fast_q <= ld_fast_d;
        room_q <= (cmd == "C") || (f_cnt < 9'd240);
        rep_room_q <= f_cnt < 9'd250;
        all_out <= (done == n);              // one clock late; done only grows during a run
        cyc_hi <= cyc_hi + cyc_c;            // (ARG below overrides these on a new command)
        cyc_c <= 1'b0;
        bv_q <= (st == RUN) && out_valid;
        bval_q <= out_data;
        if (rst) begin
            st <= IDLE;
        end else case (st)
        IDLE: if (rx_v) begin
            cmd <= rx_d; argi <= 0; n <= 0;
            if (rx_d == "I") begin
                rep[0] <= "I"; rep[1] <= 8'(`FOLD_P); rep[2] <= 8'(MHZ); rep[3] <= 8'(MHZ >> 8);
                {rep[7], rep[6], rep[5], rep[4]} <= 32'(`FOLD_DEPTH);
                {rep[9], rep[8]} <= 16'(`FOLD_S);
                rep_n <= 10; rep_i <= 0; st <= REPLY;
            end else if (rx_d == "D") begin
                left <= 32'(`FOLD_DEPTH); l_nz <= 1'b1; st <= D_ROT;
            end else if (rx_d == "L" || rx_d == "S" || rx_d == "C") st <= ARG;
        end
        ARG: if (rx_v) begin
            n <= arg;
            argi <= argi + 1;
            if (argi == 3) begin
                left <= arg;
                l_gt2 <= arg > 2; l_gt1 <= arg > 1; l_eq1 <= arg == 1; l_nz <= arg != 0;
                done <= 0; crc <= 32'hFFFFFFFF; cyc_lo <= 0; cyc_hi <= 0; cyc_c <= 1'b0;
                bcnt <= 0; bmax <= 0; bmax_at <= 0;
                st <= (cmd == "L") ? LOAD_LO : RUN;
            end
        end
        LOAD_LO: if (!l_nz) begin rep[0] <= "l"; rep_n <= 1; rep_i <= 0; st <= REPLY; end
                 else if (rx_v) begin lo <= rx_d; st <= LOAD_HI; end
        LOAD_HI: if (rx_v) begin
            ld_data <= {rx_d[0], lo};
            if (l_gt2) begin                                           // fast shift next clock
                left <= left - 1; st <= LOAD_LO;
                l_gt2 <= left > 3; l_gt1 <= 1'b1; l_eq1 <= 1'b0; l_nz <= 1'b1;
            end
            else st <= LOAD_PUT;
        end
        LOAD_PUT: if (shifting) begin                                 // left is 2 or 1
            left <= left - 1; st <= LOAD_LO;
            l_gt2 <= 1'b0; l_gt1 <= 1'b0; l_eq1 <= left == 2; l_nz <= left == 2;
        end
        RUN: begin
            if (bv_q) begin                      // the last value lands 1 clock before all_out
                bcnt <= bcnt + 1;
                if (bval_q > bmax) begin bmax <= bval_q; bmax_at <= bcnt; end
            end
            if (l_nz) {cyc_c, cyc_lo} <= {1'b0, cyc_lo} + 33'd1;
            if (shifting) begin
                left <= left - 1;
                l_gt2 <= left > 3; l_gt1 <= left > 2; l_eq1 <= left == 2; l_nz <= left != 1;
            end
            if (out_valid) begin
                crc <= crc16(crc, {7'd0, out_data});
                last <= out_data;
                done <= done + 1;
                if (cmd == "S") begin q_wr <= 1'b1; q_d <= out_data[7:0]; end
            end
            if (!l_nz && all_out) begin
                if (cmd == "S") begin rep[0] <= "s"; rep[1] <= {7'd0, err}; rep_n <= 2; end
                else begin
                    rep[0] <= "c";
                    {rep[4], rep[3], rep[2], rep[1]} <= ~crc;
                    {rep[12], rep[11], rep[10], rep[9], rep[8], rep[7], rep[6], rep[5]} <= {cyc_hi, cyc_lo};
                    {rep[14], rep[13]} <= {7'd0, last};
                    rep[15] <= {7'd0, err};
                    {rep[17], rep[16]} <= {7'd0, bmax};
                    {rep[21], rep[20], rep[19], rep[18]} <= bmax_at;
                    rep_n <= 22;
                end
                rep_i <= 0; st <= REPLY;
            end
        end
        // dump: one rotation step per value; tail is H[DEPTH-1] as of the previous clock, and
        // the last shift was 3 clocks ago, so it is the entry the shift in the next clock moves out
        D_ROT: if (!l_nz) begin rep[0] <= "d"; rep_n <= 1; rep_i <= 0; st <= REPLY; end
               else if (rep_room_q) begin ld_data <= tail_v; st <= D_LO; end
        D_LO:  begin q_wr <= 1'b1; q_d <= ld_data[7:0]; st <= D_HI; end
        D_HI:  begin
            q_wr <= 1'b1; q_d <= {7'd0, ld_data[8]};
            left <= left - 1; l_nz <= left != 1; st <= D_ROT;
        end
        REPLY: if (rep_i == rep_n) st <= IDLE;
               else if (rep_room_q) begin q_wr <= 1'b1; q_d <= rep[rep_i]; rep_i <= rep_i + 1; end
        default: st <= IDLE;
        endcase
        // the high byte of a streamed value goes one clock after the low byte
        if (st == RUN && cmd == "S" && hi_pending) begin q_wr <= 1'b1; q_d <= {7'd0, hi_bit}; end
    end

    // ---- highest nim-value so far (loaded and computed), on the 7-segment display ----
    reg  [8:0] gmax;
    wire       g_new = out_valid | ld_fast_q | (ld_slow & shifting);
    wire [8:0] g_val = out_valid ? out_data : ld_data;
    reg        g_new_q = 1'b0;           // registered first: the compare is not timing-critical
    reg  [8:0] g_val_q;
    always @(posedge clk) begin
        g_new_q <= g_new;
        g_val_q <= g_val;
        if (rst || (st == IDLE && rx_v && rx_d == "L")) gmax <= 9'd0;
        else if (g_new_q && g_val_q > gmax) gmax <= g_val_q;
    end
    seg7_dec3 u_seg (.clk(clk), .value(gmax), .seg(SEG), .dp(DP), .an(AN));

    assign LED[0] = st != IDLE;
    assign LED[1] = err;
    assign LED[2] = locked;
    assign LED[7:3] = 5'd0;
    assign LED[15:8] = done[31:24];
endmodule

// A value 0..511 in decimal on the right three digits (leading zeros blank),
// multiplexed at about 140 Hz per digit cycle.
module seg7_dec3 (
    input  wire       clk,
    input  wire [8:0] value,
    output reg  [6:0] seg,
    output wire       dp,
    output reg  [7:0] an
);
    // sequential double dabble (no division logic in the 200 MHz domain): 9 shifts,
    // then the result is latched and the next conversion starts
    reg [11:0] bcd = 0, acc = 0;
    reg [8:0]  sh = 0;
    reg [3:0]  it = 0;
    always @(posedge clk) begin
        if (it == 0) begin
            bcd <= acc; sh <= value; acc <= 0; it <= 9;
        end else begin
            logic [11:0] a;
            a = acc;
            for (int k = 0; k < 3; k++) if (a[4*k +: 4] >= 5) a[4*k +: 4] = a[4*k +: 4] + 4'd3;
            acc <= {a[10:0], sh[8]};
            sh  <= {sh[7:0], 1'b0};
            it  <= it - 1;
        end
    end
    reg [19:0] cnt = 0;
    always @(posedge clk) cnt <= cnt + 1;
    wire [2:0] d = cnt[19:17];
    reg  [3:0] nib;
    reg        on;
    always @(*) begin
        case (d)
            3'd0:    begin nib = bcd[3:0];  on = 1'b1; end
            3'd1:    begin nib = bcd[7:4];  on = bcd[11:4] != 0; end
            3'd2:    begin nib = bcd[11:8]; on = bcd[11:8] != 0; end
            default: begin nib = 4'd0;      on = 1'b0; end
        endcase
    end
    always @(posedge clk) begin
        an <= on ? ~(8'd1 << d) : 8'hFF;
        case (nib)                      // gfedcba, active low
            4'd0: seg <= ~7'h3F;  4'd1: seg <= ~7'h06;  4'd2: seg <= ~7'h5B;  4'd3: seg <= ~7'h4F;
            4'd4: seg <= ~7'h66;  4'd5: seg <= ~7'h6D;  4'd6: seg <= ~7'h7D;  4'd7: seg <= ~7'h07;
            4'd8: seg <= ~7'h7F;  4'd9: seg <= ~7'h6F;  default: seg <= 7'h7F;
        endcase
    end
    assign dp = 1'b1;
endmodule
`default_nettype wire
