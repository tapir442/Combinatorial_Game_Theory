// =============================================================================
// window_fp.sv -- 64-bit fingerprint of the speculation core's history window,
// for the period search.
//
// -----------------------------------------------------------------------------
// Why the window
// -----------------------------------------------------------------------------
// The folded core's next value depends only on its history H, the last DEPTH
// values (DEPTH = the largest rare heap, 20,627 for .6).  So if the window at
// heap n equals the window at an earlier heap n_c, the speculated sequence
// repeats from n_c with period p = n - n_c, forever.  Comparing 20,627 values per
// heap is too much; comparing a fingerprint of them is not.
//
// -----------------------------------------------------------------------------
// The fingerprint (Rabin, over GF(2))
// -----------------------------------------------------------------------------
// Read each 9-bit value w as a polynomial w(x) of degree < 9.  For the window
// w_0 = G(n-1) (newest) .. w_{D-1} = G(n-D) (oldest):
//
//     fp = sum_j w_j(x) * x^(9j)   mod P(x),     P = x^64 + PLOW
//
// P is irreducible of degree 64, chosen at random (PLOW below; seed 20261118,
// Rabin's test: x^(2^64) = x mod P and gcd(x^(2^32) - x, P) = 1).  Two different
// windows have the same fp only if P divides the difference of their
// polynomials (degree < 9D); for a random irreducible P that has probability
// about 9D / 2^64 ~ 1e-14, and no structured difference (swaps, bursts) is
// special.  A hit is confirmed on the host by comparing whole windows anyway.
//
// When a value v enters the window and `old` (= w_{D-1}) leaves it:
//
//     fp' = fp * x^9  +  v  +  old * x^(9D)       mod P   (+ is XOR)
//
// * fp * x^9 mod P: shift by 9, folding the 9 bits that pass x^64 back in with P:
//   a fixed XOR network, at most ~10 inputs per bit.
// * old * x^(9D) mod P: linear in the 9 bits of old, so the XOR of the constants
//   T[i] = x^(9D+i) mod P for the set bits.  The constants are computed at
//   elaboration (powx, square and multiply).
// No carries, no DSPs: 64 bits of 2-3 LUT levels.
//
// -----------------------------------------------------------------------------
// Where `old` comes from: a mirror of the window in block RAM
// -----------------------------------------------------------------------------
// The top level fires `ev` once per history shift, in shift order, with the value
// shifted in (computed values, fast and slow loads, dump rotations -- its g_new /
// g_val).  This module keeps its own ring of the last DEPTH of those values, so
// the value leaving the core's window is the one leaving the ring.  Nothing
// depends on the core's internal timing.
//
// Invariant: fp = fingerprint(ring contents, in order).  Both start as zeros
// (BRAM and register initial values) and are updated together, so it holds
// from power-up.  They must NOT be reset separately: after a reset of fp alone
// the difference would be multiplied by x^9 at every step and never vanish.
// After an 'L' of at least DEPTH values the ring equals the core's window.
//
// -----------------------------------------------------------------------------
// Pipeline (one event per clock is accepted; the board makes at most one per 2)
// -----------------------------------------------------------------------------
//   clock t    ev, v, comp presented; the ring is read (old_r) and written (v)
//              at ptr in the same clock (READ_FIRST), ptr moves on
//   clock t+1  old_r goes through the block RAM's output register (old_q):
//              the RAM's clock-to-out does not reach the XOR network
//   clock t+2  fp computed from v_dd and old_q; written on the edge that ends
//              t+2, together with the pulses upd (and upd_comp if the value was
//              computed, not loaded)
//   clock t+3  the new fp is visible, upd / upd_comp are high (one clock)
// =============================================================================
`default_nettype none
module window_fp #(
    parameter int          DEPTH = 20627,                 // window length (the core's FOLD_DEPTH)
    parameter logic [63:0] PLOW  = 64'h1028ecf30eb3f40d   // P = x^64 + PLOW, irreducible
) (
    input  wire        clk,
    input  wire        ev,          // a value enters the window this clock
    input  wire        comp,        // ... and it was computed by the core (not loaded)
    input  wire  [8:0] v,           // the value
    output reg  [63:0] fp = 64'd0,  // fingerprint of the window, see above
    output reg         upd = 1'b0,      // fp was just updated: high in the first clock of the new fp
    output reg         upd_comp = 1'b0  // ... for a computed value
);
    // ---- GF(2)[x] / P arithmetic, for elaboration and for the update ----

    // h * x mod P: shift; if x^64 appears, replace it by PLOW
    function automatic logic [63:0] mulx(input logic [63:0] h);
        return {h[62:0], 1'b0} ^ (h[63] ? PLOW : 64'd0);
    endfunction

    // h * x^9 mod P (the per-shift multiply; synthesizes to a fixed XOR network)
    function automatic logic [63:0] mulx9(input logic [63:0] h);
        for (int k = 0; k < 9; k++) h = mulx(h);
        return h;
    endfunction

    // a * b mod P (elaboration only), Horner over the bits of b
    function automatic logic [63:0] mulmod(input logic [63:0] a, input logic [63:0] b);
        logic [63:0] r;
        r = 64'd0;
        for (int i = 63; i >= 0; i--) begin
            r = mulx(r);
            if (b[i]) r ^= a;
        end
        return r;
    endfunction

    // x^e mod P (elaboration only), square and multiply
    function automatic logic [63:0] powx(input longint unsigned e);
        logic [63:0] r, b;
        r = 64'd1;
        b = 64'd2;                                  // the polynomial x
        while (e != 0) begin
            if (e & 1) r = mulmod(r, b);
            b = mulmod(b, b);
            e >>= 1;
        end
        return r;
    endfunction

    // T[i] = x^(9*DEPTH + i) mod P: the weight of bit i of the value leaving the window
    function automatic logic [8:0][63:0] out_weights();
        logic [8:0][63:0] t;
        for (int i = 0; i < 9; i++) t[i] = powx(longint'(9) * DEPTH + i);
        return t;
    endfunction
    localparam logic [8:0][63:0] T = out_weights();

    // ---- the ring: the last DEPTH values, in block RAM ----
    (* ram_style = "block" *) reg [8:0] ring [0:DEPTH-1];
    initial for (int i = 0; i < DEPTH; i++) ring[i] = 9'd0;   // = BRAM power-up contents
    reg [$clog2(DEPTH)-1:0] ptr = 0;                // oldest entry = the next to be replaced
    reg [8:0] old_r = 9'd0;                         // value that left (RAM latch)
    reg [8:0] old_q = 9'd0;                         // ... through the RAM output register
    reg [8:0] v_d = 9'd0, v_dd = 9'd0;              // value that entered, 1 and 2 clocks late
    reg       ev_d = 1'b0, ev_dd = 1'b0, comp_d = 1'b0, comp_dd = 1'b0;

    // clock t: read the oldest value and overwrite it with the new one (read-first);
    // clock t+1: the RAM output register
    always_ff @(posedge clk) begin
        {ev_dd, ev_d}     <= {ev_d, ev};
        {comp_dd, comp_d} <= {comp_d, comp};
        v_d  <= v;
        v_dd <= v_d;
        if (ev) begin
            old_r     <= ring[ptr];
            ring[ptr] <= v;
            ptr       <= (ptr == DEPTH - 1) ? '0 : ptr + 1'b1;
        end
        if (ev_d) old_q <= old_r;
    end

    // clock t+2: fp' = fp * x^9 + v + old * x^(9D)
    always_ff @(posedge clk) begin
        upd      <= ev_dd;
        upd_comp <= ev_dd & comp_dd;
        if (ev_dd) begin
            logic [63:0] o;
            o = 64'd0;
            for (int i = 0; i < 9; i++) if (old_q[i]) o ^= T[i];
            fp <= mulx9(fp) ^ {55'd0, v_dd} ^ o;
        end
    end
endmodule
`default_nettype wire
