module others;

default clocking @(posedge tb_top.clock);
endclocking

// `tb_top` is the top-level testbench module
property TestProperty();  disable iff(tb_top.reset) tb_top.uut.counter[0] && tb_top.uut.another_reg; endproperty
cover_test: cover property (TestProperty);

endmodule