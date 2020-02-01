/* Copyright 2018 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * This example requires a design-arty bsp that has switches as inputs into the PLIC.
 * This heirarchy can be checked in the design.dts file.
 */

#include <stdio.h>
#include <stdlib.h>

/* These includes get created at build time, and are based on the contents
 * in the bsp folder.  They are useful since they allow us
 * to use auto generated symbols and base addresses which may change
 * based on the design.
 */
#include <metal/machine.h>
#include <metal/machine/platform.h>
#include <metal/machine/inline.h>

/*
 * This test demonstrates how to enable and handle a global interrupt that
 * is managed by the Platform Level Interrupt Controller (PLIC), and routed
 * into the CPU through the local external interrupt connection, which
 * has interrupt ID #11.
 *
 * At the CPU level, we configure CLINT vectored mode of operation, which
 * allows lower latency to handle any local interrupt into the CPU.
 */

#if __riscv_xlen == 32
#define MCAUSE_INTR                         0x80000000UL
#define MCAUSE_CAUSE                        0x000003FFUL
#else
#define MCAUSE_INTR                         0x8000000000000000UL
#define MCAUSE_CAUSE                        0x00000000000003FFUL
#endif
#define MCAUSE_CODE(cause)                  (cause & MCAUSE_CAUSE)

/* Compile time options to determine which interrupt modules we have */
#define CLINT_PRESENT                           (METAL_MAX_CLINT_INTERRUPTS > 0)
#define CLIC_PRESENT                            (METAL_MAX_CLIC_INTERRUPTS > 0)
#define PLIC_PRESENT                            (METAL_MAX_PLIC_INTERRUPTS > 0)

#define DISABLE              0
#define ENABLE               1
#define RTC_FREQ            32768

/* Interrupt Specific defines - used for mtvec.mode field, which is bit[0] for
 * designs with CLINT, or [1:0] for designs with a CLIC */
#define MTVEC_MODE_CLINT_DIRECT                 0x00
#define MTVEC_MODE_CLINT_VECTORED               0x01
#define MTVEC_MODE_CLIC_DIRECT                  0x02
#define MTVEC_MODE_CLIC_VECTORED                0x03

/* PLIC base address, see: bsp/metal-platform.h */
#define PLIC_BASE_ADDR                          METAL_RISCV_PLIC0_0_BASE_ADDRESS
#define PLIC_PRIORITY_ADDR(plic_int)            (PLIC_BASE_ADDR + (METAL_RISCV_PLIC0_PRIORITY_BASE) + (4*plic_int))
#define PLIC_PENDING_BASE_ADDR                  (PLIC_BASE_ADDR + METAL_RISCV_PLIC0_PENDING_BASE)
#define PLIC_ENABLE_BASE_ADDR                   (PLIC_BASE_ADDR + METAL_RISCV_PLIC0_ENABLE_BASE)
#define PLIC_THRESHOLD_ADDR                     (PLIC_BASE_ADDR + METAL_RISCV_PLIC0_THRESHOLD)
#define PLIC_CLAIM_COMPLETE_ADDR                (PLIC_BASE_ADDR + METAL_RISCV_PLIC0_CLAIM)

void plic_enable_disable(uint32_t int_id, uint32_t en_dis);
uint32_t plic_pending (uint32_t int_id);
void interrupt_global_enable (void);
void interrupt_global_disable (void);
void interrupt_software_enable (void);
void interrupt_software_disable (void);
void interrupt_timer_enable (void);
void interrupt_timer_disable (void);
void interrupt_external_enable (void);
void interrupt_external_disable (void);
void interrupt_local_enable (int id);

/* Defines to access CSR registers within C code */
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

#define write_csr(reg, val) ({ \
  asm volatile ("csrw " #reg ", %0" :: "rK"(val)); })

#define MTIMECMP_ADDR                           0x02004000  /* standard base address for MTIMECMP - check your design */
#define write_dword(addr, data)                 ((*(uint64_t *)(addr)) = data)
#define read_dword(addr)                         (*(uint64_t *)(addr))
#define write_word(addr, data)                  ((*(uint32_t *)(addr)) = data)
#define read_word(addr)                         (*(uint32_t *)(addr))
#define write_byte(addr, data)                  ((*(uint8_t *)(addr)) = data)
#define read_byte(addr)                         (*(uint8_t *)(addr))

/* Globals */
void __attribute__((weak, interrupt)) __mtvec_clint_vector_table(void);
void __attribute__((weak, interrupt)) software_handler (void);
void __attribute__((weak, interrupt)) timer_handler (void);
void __attribute__((weak, interrupt)) external_handler (void);
void __attribute__((weak, interrupt)) default_vector_handler (void);
void __attribute__((weak)) default_exception_handler(void);
void plic_sw_handler(uint32_t);

uint32_t plic_interrupt_lines[METAL_MAX_GLOBAL_EXT_INTERRUPTS];
uint32_t timer_isr_counter = 0;

/* Main - Setup PLIC interrupt handling and describe how to trigger interrupt */
int main() {

    uint32_t i, mode = MTVEC_MODE_CLINT_VECTORED;
    uint32_t mtvec_base;

    /* Write mstatus.mie = 0 to disable all machine interrupts prior to setup */
    interrupt_global_disable();

    /* Setup mtvec to point to our exception handler table using mtvec.base,
     * and assign mtvec.mode = 1 for CLINT vectored mode of operation. The
     * mtvec.mode field is bit[0] for designs with CLINT, or [1:0] using CLIC */
    mtvec_base = (uint32_t)&__mtvec_clint_vector_table;
    write_csr (mtvec, (mtvec_base | mode));

#if PLIC_PRESENT
    /* Setup PLIC based on interrupt lines in this design */
    for (i = 0; i < METAL_MAX_GLOBAL_EXT_INTERRUPTS; i++) {

        /* Get the list of PLIC interrupts */
        plic_interrupt_lines[i] = __metal_driver_sifive_global_external_interrupts0_interrupt_lines(NULL, i);

        /* Write enable register for each interrupt */
        plic_enable_disable(plic_interrupt_lines[i], ENABLE);

        /* Set Priority - valid values are 1 - 7.  A value of 0 means disabled */
        write_word(PLIC_PRIORITY_ADDR(plic_interrupt_lines[i]), 0x2);
    }
#else
#error "This design does not have a PLIC...Exiting."
    exit(0x77);
#endif

    /* Set global threshold register to 01 to allow all interrupts of priority of 2 and above */
    write_word(PLIC_THRESHOLD_ADDR, 0x1);

    /* Enable External interrupts in mie register.
     * Software, timer, and local interrupts 16-31 also live here */
    interrupt_external_enable();

    /* Write mstatus.mie = 1 to enable all machine interrupts */
    interrupt_global_enable();

    /* Allow user to play with switches to trigger interrupt - need COMx
     * connection for serial data on Arty board, coming from printf */
    while (1);
}

/* External Interrupt ID #11 - handles all global interrupts from PLIC */
void __attribute__((weak, interrupt)) external_handler (void) {

    /* read PLIC claim register */
    int claim_complete_id = read_word(PLIC_CLAIM_COMPLETE_ADDR);

    if (claim_complete_id != 0) {
        printf ("Handling PLIC Interrupt ID: %d\n", claim_complete_id);

        /* Call interrupt specific software function (Or call s/w table function here) */
        plic_sw_handler(claim_complete_id);
    }
    else {
        printf ("PLIC Interrupt claim of 0x0 - interrupt already claimed!\n");
    }

    /* read pending register */
    int plic_int_pend = plic_pending (claim_complete_id);

    /* If we have a valid claim ID then check if pending has gone low */
    if ((plic_int_pend != 0) && (claim_complete_id != 0)) {
        printf ("PLIC Pending Interrupt %d Not clear!\n", claim_complete_id);
    }

    if (claim_complete_id != 0) {
        /* write it back to complete interrupt */
        write_word(PLIC_CLAIM_COMPLETE_ADDR, claim_complete_id);
    }
}

void __attribute__((weak, interrupt)) software_handler (void) {
    /* Add functionality if desired */
}

void __attribute__((weak, interrupt)) timer_handler (void) {
    uintptr_t mtime = read_csr(time);

    printf ("Timer Handler! Count: %d\n", timer_isr_counter++);
    mtime += 100*RTC_FREQ;
    write_dword(MTIMECMP_ADDR, mtime);    /* next timer interrupt is sometime in the future */
}

void __attribute__((weak, interrupt)) default_vector_handler (void) {
    /* Add functionality if desired */
    while (1);
}

void __attribute__((weak)) default_exception_handler(void) {

    /* Read mcause to understand the exception type */
    uint32_t mcause = read_csr(mcause);
    uint32_t mepc = read_csr(mepc);
    uint32_t mtval = read_csr(mtval);
    uint32_t code = MCAUSE_CODE(mcause);

    printf ("Exception Hit! mcause: 0x%08x, mepc: 0x%08x, mtval: 0x%08x\n", mcause, mepc, mtval);
    printf ("Mcause Exception Code: 0x%08x\n", code);
    printf("Now Exiting...\n");

    /* Exit here using non-zero return code */
    exit (0xEE);
}

/* Global software support for different interrupts */
void plic_sw_handler(uint32_t plic_id) {

    if (plic_id == plic_interrupt_lines[0]) {
        /* Add customization as needed depending on global interrupt source */
    }
    //else if...
}

/* One enable bit per interrupt, 4B interrupt enable registers */
void plic_enable_disable(uint32_t int_id, uint32_t en_dis) {

    uint32_t reg = int_id / 32;      /* get index */
    uint32_t bitshift = int_id % 32; /* remainder is bit position */
    uint32_t plic_enable_addr = PLIC_ENABLE_BASE_ADDR + 4*reg;
    uint32_t enable_reg = read_word (plic_enable_addr);

    /* Enable or Disable? */
    if (int_id < 128) {
        if (en_dis != 0) {
            enable_reg |= (1 << bitshift);
        }
        else {
            enable_reg &= ~(1 << bitshift);
        }

        /* Write it back */
        write_word (plic_enable_addr, enable_reg);
    }
}

/* Read PLIC pending bit for a certain interrupt
 * return 0x1 if pending
 * return 0x0 if not pending
 */
uint32_t plic_pending (uint32_t int_id) {

    uint32_t reg = int_id / 32;      /* get index */
    uint32_t bitshift = int_id % 32; /* remainder is bit position */
    uint32_t plic_pending_addr = PLIC_PENDING_BASE_ADDR + 4*reg;
    uint32_t pending_reg = read_word (plic_pending_addr);

    /* return single bit for pending status */
    pending_reg >>= bitshift;
    return (pending_reg & 0x1);
}

void interrupt_global_enable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrs %0, mstatus, %1" : "=r"(m) : "r"(METAL_MIE_INTERRUPT));
}

void interrupt_global_disable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrc %0, mstatus, %1" : "=r"(m) : "r"(METAL_MIE_INTERRUPT));
}

void interrupt_software_enable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrs %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_SW));
}

void interrupt_software_disable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrc %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_SW));
}

void interrupt_timer_enable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrs %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_TMR));
}

void interrupt_timer_disable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrc %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_TMR));
}

void interrupt_external_enable (void) {
    uintptr_t m;
    __asm__ volatile ("csrrs %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_EXT));
}

void interrupt_external_disable (void) {
    unsigned long m;
    __asm__ volatile ("csrrc %0, mie, %1" : "=r"(m) : "r"(METAL_LOCAL_INTERRUPT_EXT));
}

void interrupt_local_enable (int id) {
    uintptr_t b = 1 << id;
    uintptr_t m;
    __asm__ volatile ("csrrs %0, mie, %1" : "=r"(m) : "r"(b));
}
