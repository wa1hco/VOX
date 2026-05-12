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

/* Cortex-M4 system handlers — weak so a board's main can override. */
__attribute__((weak, alias("Default_Handler"))) void NMI_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void HardFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void MemManage_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void BusFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void UsageFault_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void SVC_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void DebugMon_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void PendSV_Handler(void);
__attribute__((weak, alias("Default_Handler"))) void SysTick_Handler(void);

/* STM32G4 peripheral IRQs up to and including USB_LP (slot 20).
 *
 * Each gets a weak alias to Default_Handler so any of them can be
 * overridden by simply defining a strong symbol of the same name in
 * a driver file (TinyUSB does this for USB_LP_IRQHandler via our
 * usb_irq_glue.c).
 *
 * Slots above USB_LP fall through to the linker's automatic zero-pad
 * (vector table is short of the full ~104 IRQs); adding a real handler
 * there means extending this list and the table below to match.
 */
__attribute__((weak, alias("Default_Handler"))) void WWDG_IRQHandler(void);                /*  0 */
__attribute__((weak, alias("Default_Handler"))) void PVD_PVM_IRQHandler(void);             /*  1 */
__attribute__((weak, alias("Default_Handler"))) void RTC_TAMP_LSECSS_IRQHandler(void);     /*  2 */
__attribute__((weak, alias("Default_Handler"))) void RTC_WKUP_IRQHandler(void);            /*  3 */
__attribute__((weak, alias("Default_Handler"))) void FLASH_IRQHandler(void);               /*  4 */
__attribute__((weak, alias("Default_Handler"))) void RCC_IRQHandler(void);                 /*  5 */
__attribute__((weak, alias("Default_Handler"))) void EXTI0_IRQHandler(void);               /*  6 */
__attribute__((weak, alias("Default_Handler"))) void EXTI1_IRQHandler(void);               /*  7 */
__attribute__((weak, alias("Default_Handler"))) void EXTI2_IRQHandler(void);               /*  8 */
__attribute__((weak, alias("Default_Handler"))) void EXTI3_IRQHandler(void);               /*  9 */
__attribute__((weak, alias("Default_Handler"))) void EXTI4_IRQHandler(void);               /* 10 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel1_IRQHandler(void);       /* 11 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel2_IRQHandler(void);       /* 12 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel3_IRQHandler(void);       /* 13 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel4_IRQHandler(void);       /* 14 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel5_IRQHandler(void);       /* 15 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel6_IRQHandler(void);       /* 16 */
__attribute__((weak, alias("Default_Handler"))) void DMA1_Channel7_IRQHandler(void);       /* 17 */
__attribute__((weak, alias("Default_Handler"))) void ADC1_2_IRQHandler(void);              /* 18 */
__attribute__((weak, alias("Default_Handler"))) void USB_HP_IRQHandler(void);              /* 19 */
__attribute__((weak, alias("Default_Handler"))) void USB_LP_IRQHandler(void);              /* 20 */
__attribute__((weak, alias("Default_Handler"))) void FDCAN1_IT0_IRQHandler(void);          /* 21 */
__attribute__((weak, alias("Default_Handler"))) void FDCAN1_IT1_IRQHandler(void);          /* 22 */
__attribute__((weak, alias("Default_Handler"))) void EXTI9_5_IRQHandler(void);             /* 23 */
__attribute__((weak, alias("Default_Handler"))) void TIM1_BRK_TIM15_IRQHandler(void);      /* 24 */
__attribute__((weak, alias("Default_Handler"))) void TIM1_UP_TIM16_IRQHandler(void);       /* 25 */
__attribute__((weak, alias("Default_Handler"))) void TIM1_TRG_COM_TIM17_IRQHandler(void);  /* 26 */
__attribute__((weak, alias("Default_Handler"))) void TIM1_CC_IRQHandler(void);             /* 27 */
__attribute__((weak, alias("Default_Handler"))) void TIM2_IRQHandler(void);                /* 28 */
__attribute__((weak, alias("Default_Handler"))) void TIM3_IRQHandler(void);                /* 29 */
__attribute__((weak, alias("Default_Handler"))) void TIM4_IRQHandler(void);                /* 30 */
__attribute__((weak, alias("Default_Handler"))) void I2C1_EV_IRQHandler(void);             /* 31 */
__attribute__((weak, alias("Default_Handler"))) void I2C1_ER_IRQHandler(void);             /* 32 */
__attribute__((weak, alias("Default_Handler"))) void I2C2_EV_IRQHandler(void);             /* 33 */
__attribute__((weak, alias("Default_Handler"))) void I2C2_ER_IRQHandler(void);             /* 34 */
__attribute__((weak, alias("Default_Handler"))) void SPI1_IRQHandler(void);                /* 35 */
__attribute__((weak, alias("Default_Handler"))) void SPI2_IRQHandler(void);                /* 36 */
__attribute__((weak, alias("Default_Handler"))) void USART1_IRQHandler(void);              /* 37 */
__attribute__((weak, alias("Default_Handler"))) void USART2_IRQHandler(void);              /* 38 */

/*
 * Vector table.  The placement section name (.isr_vector) is matched by
 * the linker script and put at the start of FLASH.  The first word holds
 * the initial stack pointer; the rest are exception/interrupt handler
 * addresses.  Extends through slot 20 (USB_LP); add more slots here when
 * we need handlers above that.
 */
__attribute__((section(".isr_vector"), used))
const void * const g_vector_table[] = {
    /* Cortex-M system handlers (positions 0..15) */
    (const void *)&_estack,            /*  0: initial MSP                   */
    (const void *)Reset_Handler,       /*  1: reset                         */
    (const void *)NMI_Handler,         /*  2: NMI                           */
    (const void *)HardFault_Handler,   /*  3: hard fault                    */
    (const void *)MemManage_Handler,   /*  4: memory management fault       */
    (const void *)BusFault_Handler,    /*  5: bus fault                     */
    (const void *)UsageFault_Handler,  /*  6: usage fault                   */
    0, 0, 0, 0,                        /*  7..10: reserved                  */
    (const void *)SVC_Handler,         /* 11: SVCall                        */
    (const void *)DebugMon_Handler,    /* 12: debug monitor                 */
    0,                                 /* 13: reserved                      */
    (const void *)PendSV_Handler,      /* 14: PendSV                        */
    (const void *)SysTick_Handler,     /* 15: SysTick                       */

    /* Peripheral IRQs (NVIC IRQn 0..20 → vector slots 16..36) */
    (const void *)WWDG_IRQHandler,             /* IRQ  0 — slot 16 */
    (const void *)PVD_PVM_IRQHandler,          /* IRQ  1 — slot 17 */
    (const void *)RTC_TAMP_LSECSS_IRQHandler,  /* IRQ  2 — slot 18 */
    (const void *)RTC_WKUP_IRQHandler,         /* IRQ  3 — slot 19 */
    (const void *)FLASH_IRQHandler,            /* IRQ  4 — slot 20 */
    (const void *)RCC_IRQHandler,              /* IRQ  5 — slot 21 */
    (const void *)EXTI0_IRQHandler,            /* IRQ  6 — slot 22 */
    (const void *)EXTI1_IRQHandler,            /* IRQ  7 — slot 23 */
    (const void *)EXTI2_IRQHandler,            /* IRQ  8 — slot 24 */
    (const void *)EXTI3_IRQHandler,            /* IRQ  9 — slot 25 */
    (const void *)EXTI4_IRQHandler,            /* IRQ 10 — slot 26 */
    (const void *)DMA1_Channel1_IRQHandler,    /* IRQ 11 — slot 27 */
    (const void *)DMA1_Channel2_IRQHandler,    /* IRQ 12 — slot 28 */
    (const void *)DMA1_Channel3_IRQHandler,    /* IRQ 13 — slot 29 */
    (const void *)DMA1_Channel4_IRQHandler,    /* IRQ 14 — slot 30 */
    (const void *)DMA1_Channel5_IRQHandler,    /* IRQ 15 — slot 31 */
    (const void *)DMA1_Channel6_IRQHandler,    /* IRQ 16 — slot 32 */
    (const void *)DMA1_Channel7_IRQHandler,    /* IRQ 17 — slot 33 */
    (const void *)ADC1_2_IRQHandler,           /* IRQ 18 — slot 34 */
    (const void *)USB_HP_IRQHandler,           /* IRQ 19 — slot 35 */
    (const void *)USB_LP_IRQHandler,           /* IRQ 20 — slot 36 */
    (const void *)FDCAN1_IT0_IRQHandler,       /* IRQ 21 — slot 37 */
    (const void *)FDCAN1_IT1_IRQHandler,       /* IRQ 22 — slot 38 */
    (const void *)EXTI9_5_IRQHandler,          /* IRQ 23 — slot 39 */
    (const void *)TIM1_BRK_TIM15_IRQHandler,   /* IRQ 24 — slot 40 */
    (const void *)TIM1_UP_TIM16_IRQHandler,    /* IRQ 25 — slot 41 */
    (const void *)TIM1_TRG_COM_TIM17_IRQHandler, /* IRQ 26 — slot 42 */
    (const void *)TIM1_CC_IRQHandler,          /* IRQ 27 — slot 43 */
    (const void *)TIM2_IRQHandler,             /* IRQ 28 — slot 44 */
    (const void *)TIM3_IRQHandler,             /* IRQ 29 — slot 45 */
    (const void *)TIM4_IRQHandler,             /* IRQ 30 — slot 46 */
    (const void *)I2C1_EV_IRQHandler,          /* IRQ 31 — slot 47 */
    (const void *)I2C1_ER_IRQHandler,          /* IRQ 32 — slot 48 */
    (const void *)I2C2_EV_IRQHandler,          /* IRQ 33 — slot 49 */
    (const void *)I2C2_ER_IRQHandler,          /* IRQ 34 — slot 50 */
    (const void *)SPI1_IRQHandler,             /* IRQ 35 — slot 51 */
    (const void *)SPI2_IRQHandler,             /* IRQ 36 — slot 52 */
    (const void *)USART1_IRQHandler,           /* IRQ 37 — slot 53 */
    (const void *)USART2_IRQHandler,           /* IRQ 38 — slot 54 */
    /* IRQs above 38 zero-pad implicitly (no slot here = handler unreachable) */
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
