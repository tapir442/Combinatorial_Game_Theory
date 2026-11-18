// =============================================================================
// check_group.sv -- split-check lanes for Officers (.6)
//
// STATUS: first cut, for resource and clock estimates only (synth_check.tcl).
// Nothing instantiates it yet; the controller, the block-RAM prefix and window
// and the DDR2 fallback are still to come.  Model: split_model.c, layout C.
//
// -----------------------------------------------------------------------------
// What is checked
// -----------------------------------------------------------------------------
// Heap n, m = n - 1.  The options of n are the pairs (a, m - a), with nim-value
// G(a) ^ G(m - a).  The speculation core computes g = the smallest *common*
// value not reached by step 1 (pairs in which a is one of the rare heaps, all
// below N0 = 50000).  g is the true G(n) iff in addition every *rare-class*
// value v < g is reached by some pair.  For n >= N0 step 1 never reaches a rare
// value (split_model.c verifies this: the rare heaps' partners G(m - r) are all
// common, and rare ^ common = common).  So this check is all of step 2:
//
//     for every rare-class v < g there is an a >= 1 with G(a) ^ G(m - a) = v.
//
// It also uses the values G(m - a) that the speculation core produced itself.
// That is sound by induction: if every heap up to n - 1 passed, they are all
// correct, so the check of heap n reads correct values.
//
// The pairs are scanned in order a = 1, 2, 3, ...  The *witness depth* is the a
// of the last missing value; on average 2,753, so a lane stops long before any
// fixed tap count K would (split_model.c, layout B vs. A).
//
// -----------------------------------------------------------------------------
// Rare classes and the 8-bit index
// -----------------------------------------------------------------------------
// v is rare-class iff popcount(v & 0x1EE) is even.  The mask 0x1EE covers bits
// 1, 2, 3, 5, 6, 7, 8, so for a rare-class v the top bit is fixed by the rest:
//
//     v[8] = ^{v[7:5], v[3:1]}          (bits 0 and 4 are free)
//
// Hence the 256 rare-class values below 512 are in one-to-one correspondence
// with their low byte v[7:0], and an 8-bit index is enough.  All values of .6
// seen so far are below 512 (the run maximum is 387), so 9-bit values suffice.
//
// -----------------------------------------------------------------------------
// Lockstep group (layout C)
// -----------------------------------------------------------------------------
// GS lanes check GS consecutive heaps together: lane l has m = m0 + l.  Each
// clock all lanes take the same W pairs a .. a+W-1.  So:
//
//   * the prefix values G(a + w), w < W, are the same for every lane: one read
//     of W values from the (constant) prefix RAM, broadcast to the group;
//   * lane l needs G(m0 + l - a - w).  Over all l and w these are the GS + W - 1
//     consecutive values G(m0 + GS-1 - a - j), j = 0 .. GS+W-2: one wide read
//     from the window RAM.
//
// The price: the group goes on until its *deepest* heap is done (efficiency
// 0.47 at GS = 16 in the model, 0.62 at GS = 4).
//
// Per lane and clock: W XORs, W rare-class tests, and a 256-bit "missing" set
// from which the W reached indices are removed.  The lane is done when the set
// is empty.
//
// Pipeline (2 clocks from the reads to `missing`, 1 more to `done`):
//
//   clock t     pre / win / step presented (the RAM outputs)
//   clock t+1   x_q = G(a+w) ^ G(m-a-w), ok_q = step and x rare-class
//   clock t+2   missing loses the indices x_q[w][7:0] with ok_q[w]
//   clock t+3   done_q = (missing is empty)
//
// So the controller sees `done` 3 clocks after the read that found the last
// value, and must keep scanning (or ignore extra steps -- they do no harm:
// clearing an already-clear bit changes nothing) until all GS lanes are done.
// =============================================================================
`default_nettype none
module check_group #(
    parameter int GS = 16,                 // lanes in the group = consecutive heaps
    parameter int W  = 2                   // pairs per lane and clock
) (
    input  wire                   clk,

    // ---- loading: one lane per clock, before the scan starts ----
    input  wire                   start,   // this clock loads g of lane ld_lane
    input  wire [$clog2(GS)-1:0]  ld_lane, // which lane
    input  wire [8:0]             ld_g,    // its speculated value g

    // ---- scanning: one step of W pairs per clock ----
    input  wire                   step,    // pre and win below are valid this clock
    input  wire [W*9-1:0]         pre,     // pre[w] = G(a + w),  w = 0 .. W-1
    input  wire [(GS+W-1)*9-1:0]  win,     // win[j] = G(m0 + GS-1 - a - j)

    // ---- result ----
    output wire [GS-1:0]          done     // lane l: all rare-class values < g reached
);
    // -------------------------------------------------------------------------
    // below(g): the start value of `missing`, bit i set iff the rare-class value
    // with low byte i is smaller than g.  A constant function of g: 256
    // comparisons of a constant with g, about 1-2 LUTs each, built once per group
    // (not per lane) because the lanes are loaded one after the other.
    // -------------------------------------------------------------------------
    function automatic logic [255:0] below(input logic [8:0] g);
        logic [255:0] r;
        for (int i = 0; i < 256; i++) begin
            logic [7:0] lo;
            logic [8:0] v;
            lo = 8'(i);
            v = {^{lo[7:5], lo[3:1]}, lo};  // the rare-class value with low byte i
            r[i] = v < g;
        end
        return r;
    endfunction

    // ---- load path: register the request, then decode g into the set ----
    reg [8:0]            g_q    = 0;       // g of the lane being loaded
    reg [$clog2(GS)-1:0] lane_q = 0;       // which lane
    reg                  ld_q   = 0;       // a load is pending in this clock
    always_ff @(posedge clk) begin
        ld_q   <= start;
        g_q    <= ld_g;
        lane_q <= ld_lane;
    end
    wire [255:0] init = below(g_q);        // combinational; written into missing next clock

    // -------------------------------------------------------------------------
    // The lanes
    // -------------------------------------------------------------------------
    for (genvar l = 0; l < GS; l++) begin : lane
        reg [255:0] missing = 0;           // rare-class values < g not yet reached (by index)
        reg [8:0]   x_q  [W];              // stage 1 result: the pair's nim-value
        reg         ok_q [W];              // stage 1 result: valid step and x rare-class

        // ---- stage 1: the W pairs (a + w, m - a - w) ----
        for (genvar w = 0; w < W; w++) begin : px
            wire [8:0] p = pre[w*9 +: 9];               // G(a + w), shared by the group
            wire [8:0] h = win[(GS-1-l+w)*9 +: 9];      // G(m0 + l - a - w), see the header
            wire [8:0] x = p ^ h;                       // nim-value of the option
            always_ff @(posedge clk) begin
                x_q[w]  <= x;
                // Only rare-class values count.  common ^ common is always
                // rare-class; x is common only when G(a) is rare, i.e. a is
                // one of the rare heaps (step 1's pairs) -- and its low byte
                // would then name a wrong value, so it must be dropped.
                ok_q[w] <= step && x[8] == ^{x[7:5], x[3:1]};
            end
        end

        // ---- stage 2: load, or remove what this clock's pairs reached ----
        always_ff @(posedge clk) begin
            if (ld_q && lane_q == l)
                missing <= init;                        // new heap: everything below g missing
            else
                for (int i = 0; i < 256; i++) begin
                    // Bit i is cleared if any of the W pairs hit index i.  Per bit
                    // this is one LUT: missing[i] and W (ok, x == i) terms; Vivado
                    // shares the 8-to-256 decode of each x_q[w] across the bits.
                    logic hit;
                    hit = 0;
                    for (int w = 0; w < W; w++) hit |= ok_q[w] && x_q[w][7:0] == 8'(i);
                    if (hit) missing[i] <= 1'b0;
                end
        end

        // ---- stage 3: done = nothing missing (256-input NOR, 2 LUT levels) ----
        // Note: before its first load a lane has missing = 0 and reads as done;
        // the controller only looks at lanes it has loaded.  (A loaded lane is
        // never empty: 0 is rare-class and every g >= 2, the smallest common
        // value; the model's smallest "need" is 2.)
        reg done_q = 0;
        always_ff @(posedge clk) done_q <= ~|missing;
        assign done[l] = done_q;
    end
endmodule
`default_nettype wire
