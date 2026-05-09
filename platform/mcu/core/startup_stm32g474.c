/*
 * startup_stm32g474.c — Cortex-M4F bring-up for STM32G474.
 *
 * What this does, in order:
 *   1) The CPU starts execution at the address stored at flash word 1
 *      (Reset_Handler).  Word 0 is the initial main stack pointer; the
 *      core loads SP from there before fetching word 1.
 *   2) Reset_Handler copies .data initial values from flash (.data load
 *      address) to RAM, zeros .bss, then calls main().
 *   3) Faults and unhandled IRQs trap into Default_Handler — an infinite
 *      loop you can break in with a debugger.
 *
 * The vector table only includes the slots we actually need for first
 * bring-up: 16 system entries.  Peripheral IRQ slots default to
 * Default_Handler via weak aliasing as we add interrupt-driven drivers
 * (UART RX, ADC DMA, USB) we'll add named entries in the table.
 *
 * Written in C (not assembly) because Cortex-M boot is just a table
 * lookup — no thumb interworking and no need for asm tricks for this.
 */

#include <stdint.h>

/* Symbols supplied by the linker script. */
extern uint32_t _estack;     /* end of stack region (initial MSP value) */
extern uint32_t _sidata;     /* .data load address (in flash)            */
extern uint32_t _sdata;      /* .data start in RAM                       */
extern uint32_t _edata;      /* .data end in RAM                         */
extern uint32_t _sbss;       /* .bss start in RAM                        */
extern uint32_t _ebss;       /* .bss end in RAM                          */

extern int main(void);

/* Forward declaration: the default handler is a tight infinite loop so
 * a debugger sees a stable PC when something goes wrong. */
void Default_Handler(void);
void Reset_Handler(void);

/* Cortex-M4 system handlers — weak so a board's main can override
 * (e.g. SysTick for a tick driver later). */
__attribute__((weak, alias("Default_Handler"))) void NMI_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void HardFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void MemManage_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void BusFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void UsageFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void SVC_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void DebugMon_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void PendSV_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void SysTick_Handler(void);

/*
 * Vector table.  The placement section name (.isr_vector) is matched by
 * the linker script and put at the start of FLASH.  The first word holds
 * the initial stack pointer; the rest are exception/interrupt handler
 * addresses.  More peripheral IRQ slots will be added here over time.
 */
__attribute__((section(".isr_vector"), used))
const void * const g_vector_table[] = {
    (const void *)&_estack,         /*  0: initial MSP                   */
    (const void *)Reset_Handler,    /*  1: reset                         */
    (const void *)NMI_Handler,      /*  2: NMI                           */
    (const void *)HardFault_Handler,/*  3: hard fault                    */
    (const void *)MemManage_Handler,/*  4: memory management fault       */
    (const void *)BusFault_Handler, /*  5: bus fault                     */
    (const void *)UsageFault_Handler,/* 6: usage fault                   */
    0, 0, 0, 0,                     /*  7..10: reserved                  */
    (const void *)SVC_Handler,      /* 11: SVCall                        */
    (const void *)DebugMon_Handler, /* 12: debug monitor                 */
    0,                              /* 13: reserved                      */
    (const void *)PendSV_Handler,   /* 14: PendSV                        */
    (const void *)SysTick_Handler,  /* 15: SysTick                       */
    /* peripheral IRQ slots default to Default_Handler — table is padded
     * by linker if needed.  Add named entries here as drivers land. */
};

void Reset_Handler(void)
{
    /* Copy .data initial values from flash to RAM. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata)
        *dst++ = *src++;

    /* Zero .bss. */
    dst = &_sbss;
    while (dst < &_ebss)
        *dst++ = 0;

    /* Cortex-M4F: enable the FPU (CP10/CP11 full access).  Doing this
     * here lets main() and any code it calls use float/double freely
     * without a separate clock-or-init step.  CPACR is at 0xE000ED88. */
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88u;
    *cpacr |= (0xFu << 20);

    /* Run application. */
    (void)main();

    /* main() should not return on bare metal.  Trap if it does. */
    for (;;) { __asm__ volatile ("wfi"); }
}

void Default_Handler(void)
{
    /* Stay here so the debugger pins the failure point.  Replace with
     * a fault dumper later (PSP/MSP unwind, register snapshot, etc.). */
    for (;;) { __asm__ volatile ("wfi"); }
}

/* ----- newlib heap support ------------------------------------------ */
/*
 * _sbrk: extend the heap by `incr` bytes, return the previous break.
 * newlib's malloc/calloc/free use this as the only heap-extension call.
 *
 * The heap lives between the linker-provided `_end` symbol (top of .bss)
 * and a high-water mark `_estack - _min_stack_size`.  The linker script
 * already has a `._stack_check` section that fails the link if even
 * `_min_stack_size` doesn't fit, so this function only has to enforce
 * the upper bound at runtime as the heap grows toward the stack.
 *
 * Returns -1 (cast to caddr_t) on out-of-memory, the standard signal
 * malloc expects to translate to NULL.
 */
extern uint32_t _end;
extern uint32_t _estack;

#define VOX_HEAP_STACK_GUARD 0x4000u   /* keep 16 KB free for the stack;
                                          must match _min_stack_size in
                                          the linker script. */

void *_sbrk(int incr)
{
    static char *heap_end = 0;
    char *prev_heap_end;

    if (heap_end == 0)
        heap_end = (char *)&_end;

    char *heap_limit = (char *)&_estack - VOX_HEAP_STACK_GUARD;
    if (heap_end + incr > heap_limit)
        return (void *)-1;             /* malloc → NULL */

    prev_heap_end = heap_end;
    heap_end += incr;
    return prev_heap_end;
}
