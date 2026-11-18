// =============================================================================
// check_stream.sv -- one value per clock out of a wide, shared block RAM
//
// Part of the split check (check_cluster.sv).  A lane group consumes one value
// per clock from each of two sequences: the prefix G(0), G(1), ... ascending and
// the window G(top), G(top-1), ... descending.  Its RAM words hold 16 values
// (144 bits), and a read port is shared by NS groups in fixed time slots, so a
// group gets one word every NS clocks and must buffer it.
//
// Buffers: two words in ping-pong, b[0] and b[1], and `head` (the value offered
// now).  A word read arrives straight in the free buffer, so the wide buffer
// enables depend only on the response; consuming a value (adv) moves only the
// read position (rb, ptr) and head.  With two words buffered a stream never
// runs dry as long as NS + read latency <= 16.
//
//   req / req_w   want word req_w (high while nxt is free and nothing is pending)
//   gnt           the cluster read req_w for us this clock (our slot and req)
//   rsp_v/rsp_d   that word, LAT clocks after gnt
//   head / hv     the current value; adv consumes it (only when hv)
//   flush         start again at word start_w (ptr at its first value).  A read
//                 still pending is dropped when it arrives.
//
// Word layout: value 16w + i in bits [9i +: 9] of word w.  DESC = 1 walks a
// word from i = 15 down to 0 and the words downwards.
// =============================================================================
`default_nettype none
module check_stream #(
    parameter int AW   = 10,               // word address bits
    parameter bit DESC = 0                 // 0: ascending, 1: descending
) (
    input  wire            clk,
    input  wire            flush,
    input  wire [AW-1:0]   start_w,
    output wire            req,
    output wire [AW-1:0]   req_w,
    input  wire            gnt,
    input  wire            rsp_v,
    input  wire [143:0]    rsp_d,
    input  wire            adv,
    output wire [8:0]      head,
    output wire            hv
);
    localparam logic [3:0] FIRST = DESC ? 4'd15 : 4'd0;
    localparam logic [3:0] LAST  = DESC ? 4'd0  : 4'd15;

    reg [AW-1:0] wa   = 0;                 // next word to request
    reg          pend = 0;                 // a read is under way
    reg          drop = 0;                 // ... and it belongs to before a flush
    reg          tgt  = 0;                 // ... and the buffer it goes to
    (* ram_style = "registers" *) reg [143:0] b [2];   // flip-flops: LUT-RAM is scarce beside the core
    reg [1:0]    bv   = 0;                 // buffer holds a word not yet used up
    reg          rb   = 0;                 // buffer being read
    reg [3:0]    ptr  = 0;                 // position in it
    reg [8:0]    head_r = 0;
    reg          hv_r   = 0;
    initial begin b[0] = 0; b[1] = 0; end

    assign req   = !pend && !(&bv) && !flush;   // a buffer is free
    assign req_w = wa;
    assign head  = head_r;
    assign hv    = hv_r;

    wire load = adv || !hv_r;              // head is free (or freed now)
    wire take = load && bv[rb];            // head takes b[rb][ptr]

    // the buffer a response fills: the one not holding a word; if both are
    // free, the one read next
    wire free_b = bv[rb] ? !rb : rb;

    always_ff @(posedge clk) begin
        if (rsp_v && !drop) b[tgt] <= rsp_d;
        // head's value loads on take even in a flush clock (hv is cleared then):
        // its enable is `take` alone, one LUT less on the step path
        if (take) head_r <= b[rb][ptr*9 +: 9];
    end

    always_ff @(posedge clk) begin
        if (flush) begin
            wa   <= start_w;
            bv   <= 2'b00;
            rb   <= 1'b0;
            ptr  <= FIRST;
            hv_r <= 1'b0;
            // a read still under way is for the old sequence: drop it on arrival
            pend <= pend && !rsp_v;
            drop <= pend && !rsp_v;
        end else begin
            // ---- requests and responses ----
            if (gnt) begin
                pend <= 1'b1;
                tgt  <= free_b;
                wa   <= DESC ? wa - 1'b1 : wa + 1'b1;
            end
            if (rsp_v) begin
                pend <= 1'b0;
                if (drop) drop <= 1'b0;
                else      bv[tgt] <= 1'b1;
            end
            // ---- head ----
            if (take) begin
                hv_r   <= 1'b1;
                ptr    <= DESC ? ptr - 1'b1 : ptr + 1'b1;   // wraps to FIRST after LAST
                if (ptr == LAST) begin
                    bv[rb] <= 1'b0;        // (rsp_v never targets rb while bv[rb])
                    rb     <= !rb;
                end
            end else if (load) begin
                hv_r <= 1'b0;
            end
        end
    end
endmodule
`default_nettype wire
