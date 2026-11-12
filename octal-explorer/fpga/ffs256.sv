// Lowest set bit of a 256-bit vector: radix-4 tree (shared by the folded core;
// gen_officers.py emits the same module into officers_core.sv).
`default_nettype none
module ffs256 (input wire [255:0] x, output wire [7:0] idx, output wire any);
    wire [255:0] v0 = x;
    wire [63:0] v1;
    wire [127:0] i1;
    genvar g0;
    generate for (g0 = 0; g0 < 64; g0++) begin : l0
        wire [3:0] q = v0[4*g0 +: 4];
        wire [1:0] s = q[0] ? 2'd0 : q[1] ? 2'd1 : q[2] ? 2'd2 : 2'd3;
        assign v1[g0] = |q;
        assign i1[2*g0 +: 2] = s;
    end endgenerate
    wire [15:0] v2;
    wire [63:0] i2;
    genvar g1;
    generate for (g1 = 0; g1 < 16; g1++) begin : l1
        wire [3:0] q = v1[4*g1 +: 4];
        wire [1:0] s = q[0] ? 2'd0 : q[1] ? 2'd1 : q[2] ? 2'd2 : 2'd3;
        assign v2[g1] = |q;
        wire [1:0] sub = s == 2'd0 ? i1[2*(4*g1+0) +: 2] :
                         s == 2'd1 ? i1[2*(4*g1+1) +: 2] :
                         s == 2'd2 ? i1[2*(4*g1+2) +: 2] :
                                     i1[2*(4*g1+3) +: 2];
        assign i2[4*g1 +: 4] = {s, sub};
    end endgenerate
    wire [3:0] v3;
    wire [23:0] i3;
    genvar g2;
    generate for (g2 = 0; g2 < 4; g2++) begin : l2
        wire [3:0] q = v2[4*g2 +: 4];
        wire [1:0] s = q[0] ? 2'd0 : q[1] ? 2'd1 : q[2] ? 2'd2 : 2'd3;
        assign v3[g2] = |q;
        wire [3:0] sub = s == 2'd0 ? i2[4*(4*g2+0) +: 4] :
                         s == 2'd1 ? i2[4*(4*g2+1) +: 4] :
                         s == 2'd2 ? i2[4*(4*g2+2) +: 4] :
                                     i2[4*(4*g2+3) +: 4];
        assign i3[6*g2 +: 6] = {s, sub};
    end endgenerate
    wire [0:0] v4;
    wire [7:0] i4;
    genvar g3;
    generate for (g3 = 0; g3 < 1; g3++) begin : l3
        wire [3:0] q = v3[4*g3 +: 4];
        wire [1:0] s = q[0] ? 2'd0 : q[1] ? 2'd1 : q[2] ? 2'd2 : 2'd3;
        assign v4[g3] = |q;
        wire [5:0] sub = s == 2'd0 ? i3[6*(4*g3+0) +: 6] :
                         s == 2'd1 ? i3[6*(4*g3+1) +: 6] :
                         s == 2'd2 ? i3[6*(4*g3+2) +: 6] :
                                     i3[6*(4*g3+3) +: 6];
        assign i4[8*g3 +: 8] = {s, sub};
    end endgenerate
    assign idx = i4[7:0];
    assign any = v4[0];
endmodule
`default_nettype wire
