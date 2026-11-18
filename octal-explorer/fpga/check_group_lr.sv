// =============================================================================
// check_group_lr.sv -- split-check lanes for Officers (.6), LUT-RAM version
//
// STATUS: for resource and clock estimates (synth_check.tcl) and a functional
// test against the model (tb_check_group_lr.sv).  Nothing instantiates it yet.
//
// Same check and same lockstep group as check_group.sv (read its header first):
// GS lanes check the GS consecutive heaps m0+1 .. m0+GS, all lanes take the same
// pair position a each clock, and lane l is done once every rare-class value
// below its g has been reached by some G(a) ^ G(m0 + l - a).
//
// -----------------------------------------------------------------------------
// What is different: count instead of a 256-bit "missing" register
// -----------------------------------------------------------------------------
// check_group.sv keeps the missing values as 256 flip-flops, clears them through
// a 256-way decoder per pair and tests all 256 for empty: ~430 LUTs per lane.
// Here a lane keeps the *seen* values in distributed RAM and counts:
//
//   * seen: 256 bits, as 32 words of 8 bits in LUT-RAM (word x[7:3], bit x[2:0];
//     the 8-bit index of a rare-class value is its low byte, see check_group.sv).
//     Read asynchronously, written on the clock edge: a read-modify-write per
//     clock, with no forwarding -- a read in the next clock already sees the
//     write.  Simple dual port: the read address is always x, the write
//     address is x or the clear address.
//   * left: loaded with R(g) = the number of rare-class values below g, and
//     decremented whenever a pair reaches a value below g that was not seen yet.
//     The lane is done when left reaches 0.
//
// `seen` must be empty when a heap starts.  At the end of a heap it holds
// exactly the R(g) values below g (only those are ever written), so the lane
// clears itself: 32 clocks, one word per clock, right after it is done.  It
// is then `ready` again.  The other lanes of the group are usually still busy
// (the group waits for its deepest heap), so only the deepest lane's clear
// delays the next group: 32 clocks on a mean group maximum of ~4,400.
//
// One pair per lane and clock (W = 1): a second pair would need a second RAM
// port and a same-word conflict check.  More lanes are cheaper.
//
// -----------------------------------------------------------------------------
// Rare G(a): dropped for the whole group
// -----------------------------------------------------------------------------
// A pair's value G(a) ^ G(m - a) is rare-class iff both halves are in the same
// class (the class is a parity, so it is linear).  G(m - a) is common as long as
// m - a is not a rare heap; m - a >= m / 2, so that holds for every heap
// n >= 2 * (largest rare heap) + 2 (all rare heaps of .6 are below 50,000, so
// n >= 100,000 is safe).  Then the pair is rare-class iff G(a) is common: one
// test on the broadcast prefix value, shared by the group, instead of a test
// per lane.  The controller must only use this group for such heaps.
//
// -----------------------------------------------------------------------------
// Pipeline (per lane)
// -----------------------------------------------------------------------------
//   clock t     pre / win / step presented (the RAM outputs)
//   clock t+1   registered: x_q = G(a) ^ G(m0 + l - a), lt_q = (x < g), and
//               (group) pc_q = step and G(a) common.  In this clock: word =
//               seen[x_q[7:3]] (asynchronous read); if pc_q, lt_q, busy and the
//               bit is clear, it is set on the edge ending t+1 (hit_q = 1)
//   clock t+2   left - 1 on the edge ending t+2
//   clock t+3   busy = 0 if that was the last value
//   clock t+4   done = !busy
//
// So `done` appears 4 clocks after the read of the pair that found the last
// value (check_group.sv: 3).  Extra steps after that do no harm.
// =============================================================================
`default_nettype none
module check_group_lr #(
    parameter int GS = 16                  // lanes in the group = consecutive heaps
) (
    input  wire                   clk,

    // ---- loading: all lanes at once; only when all are `ready` ----
    input  wire                   start,   // this clock loads every lane
    input  wire [GS*9-1:0]        ld_g,    // lane l: its speculated value g (>= 2)
    input  wire [GS*9-1:0]        ld_r,    // lane l: R(g), rare-class values below g
    input  wire                   abort,   // give up the heaps still busy (clear seen)

    // ---- scanning: one pair position a per clock ----
    input  wire                   step,    // pre and win below are valid this clock
    input  wire [8:0]             pre,     // G(a)
    input  wire [GS*9-1:0]        win,     // win[j] = G(m0 + GS-1 - a - j)

    // ---- result ----
    output wire [GS-1:0]          done,    // lane l: all rare-class values < g reached
    output wire [GS-1:0]          ready    // lane l: seen is clear, may be loaded
);
    // The lanes load in parallel; R(g) comes with g on the job bus (computed
    // once per value where the jobs are built: check_cluster.sv's RB table).
    // ---- the prefix value's class, shared by the group ----
    reg pc_q = 0;                          // this pair counts: step valid and G(a) common
    always_ff @(posedge clk) pc_q <= step && (^(pre & 9'h1EE));   // odd parity = common

    // -------------------------------------------------------------------------
    // The lanes
    // -------------------------------------------------------------------------
    for (genvar l = 0; l < GS; l++) begin : lane
        (* ram_style = "distributed" *) reg [7:0] seen [0:31];
        initial for (int i = 0; i < 32; i++) seen[i] = 8'd0;

        reg [8:0] g    = 0;                // this lane's g
        reg [8:0] left = 0;                // rare-class values below g not seen yet
        reg       busy = 0;                // loaded, left > 0
        reg       clr  = 0;                // clearing seen after the heap
        reg [4:0] ca   = 0;                // clear address (0 outside a clear: it wraps after 32)
        reg       rdy  = 1;                // seen is clear (true at power-up)

        // ---- stage 1: the pair's value and its comparison with g ----
        wire [8:0] x = pre ^ win[(GS-1-l)*9 +: 9];         // G(a) ^ G(m0 + l - a)
        reg  [7:0] x_q  = 0;
        reg        lt_q = 0;
        always_ff @(posedge clk) begin
            x_q  <= x[7:0];                // 8-bit index (x is rare-class here)
            lt_q <= x < g;
        end

        // ---- stage 2: read-modify-write of seen, count ----
        // The read address is x_q alone; the clear counter only drives the write
        // address (a simple-dual-port RAM), so it is not in the read path.
        // The write enable does not wait for the read: writing word | bit back
        // when the bit is already set changes nothing, so every counting pair
        // writes, and only the counter depends on whether the bit was new.
        wire [7:0] word = seen[x_q[7:3]];                 // asynchronous read
        wire       cnt  = pc_q && lt_q && busy;           // this pair counts if new
        wire       hit  = cnt && !word[x_q[2:0]];         // ... and it is new
        // The count waits one clock (hit_q): the RAM read then reaches only a
        // flip-flop, not the counter's enables.  After the last new value no
        // further hit can come (everything below g is seen), so the extra clock
        // of busy changes nothing.
        reg        hit_q = 0;
        always_ff @(posedge clk) hit_q <= hit;
        wire [4:0] wa   = clr ? ca : x_q[7:3];
        wire [7:0] wd   = clr ? 8'd0 : (word | (8'd1 << x_q[2:0]));
        always_ff @(posedge clk) begin
            if (clr || cnt) seen[wa] <= wd;
        end

        always_ff @(posedge clk) begin
            if (start) begin
                // new heap (only on a ready lane: seen is clear)
                g    <= ld_g[l*9 +: 9];
                left <= ld_r[l*9 +: 9];
                busy <= 1'b1;
                rdy  <= 1'b0;
            end else if (abort && busy) begin
                // not settled within the scan limit: the heap goes to the fallback
                busy <= 1'b0;
                clr  <= 1'b1;              // ca is 0 here
            end else if (hit_q) begin
                left <= left - 1'b1;
                if (left == 9'd1) begin    // that was the last one: done, clear seen
                    busy <= 1'b0;
                    clr  <= 1'b1;          // ca is 0 here
                end
            end else if (clr) begin
                ca <= ca + 1'b1;
                if (ca == 5'd31) begin
                    clr <= 1'b0;
                    rdy <= 1'b1;
                end
            end
        end

        // ---- stage 3: outputs ----
        reg done_q = 1, rdy_q = 1;
        always_ff @(posedge clk) begin
            done_q <= !busy;
            rdy_q  <= rdy;
        end
        assign done[l]  = done_q;
        assign ready[l] = rdy_q;
    end
endmodule
`default_nettype wire
