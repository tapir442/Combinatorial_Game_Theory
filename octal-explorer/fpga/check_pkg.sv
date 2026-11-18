// =============================================================================
// check_pkg.sv -- shared by the split-check modules
// =============================================================================
package check_pkg;
    // R(g) = the number of rare-class values (popcount(v & 0x1EE) even) below g,
    // for every 9-bit g: a constant table, computed at elaboration.  Computed
    // from g at run time it would become a 512-input adder tree.
    typedef logic [8:0] rb_t [512];
    function automatic rb_t rare_below_table();
        rb_t t;
        logic [8:0] r;
        r = 0;
        for (int g = 0; g < 512; g++) begin
            t[g] = r;                                       // count of v < g
            if (!(^(9'(g) & 9'h1EE))) r++;                  // g itself rare-class
        end
        return t;
    endfunction
    localparam rb_t RB = rare_below_table();

    // common class: popcount(v & 0x1EE) odd
    function automatic logic is_common(input logic [8:0] v);
        return ^(v & 9'h1EE);
    endfunction
endpackage
