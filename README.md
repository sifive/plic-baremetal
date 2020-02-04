# plic-baremetal
This example enables all PLIC interrupts available in the system for hart 0, based on the PLIC interrupt list in bsp/metal.h.

For SiFive standard cores such as E76, U54, and others, the Arty 100T platform can be used to demonstrate the functionality by using the switches which are inputs into the PLIC.  Flipping the switch will trigger the interrupt handler to fire, and a printf message will confirm the handler has been entered.  

This example will only be useful on designs that include interrupt lines into the PLIC that can be asserted in some fashion.

An example design.dts setup is shown below, as this demonstrates the interrupt routing:

The switch node shows interrupt 0 of L15, which is PLIC interrupt 23, below.

        switch@0 {
                compatible = "sifive,gpio-switches";
                label = "SW0";
                interrupts-extended = <&L15 0>;
                linux,code = "none";
        };


Th L15 node shows the global interupt lines that are used (23 - 30).
For this example, switch0 is connected to the first L15 instance, #23.

        L15: global-external-interrupts {
            compatible = "sifive,global-external-interrupts0";
            interrupt-parent = <&L2>;
            interrupts = <23 24 25 26 27 28 29 30>;
        };

The L2 node, the PLIC interrupt controller, describes Interrupt #11 for Machine interrupts, and #9 for Supervisor interrupts.

        L2: interrupt-controller@c000000 {
            #interrupt-cells = <1>;
            compatible = "riscv,plic0";
            interrupt-controller;
            interrupts-extended = <&L5 11 &L5 9>;
            reg = <0xc000000 0x4000000>;
            reg-names = "control";
            riscv,max-priority = <7>;
            riscv,ndev = <35>;
        };
