/*
 * stm32g474re.ld — linker script for STM32G474RET6 (Nucleo G474RE).
 *
 * 512 KB flash @ 0x08000000
 * 128 KB SRAM (split as 96 KB main + 32 KB CCM); we only use main SRAM
 *           here.  CCM-SRAM at 0x10000000 stays free for later use.
 *
 * Sections:
 *   .isr_vector  → top of FLASH         (vector table from startup .c)
 *   .text/.rodata → FLASH                (code + read-only data)
 *   .data        → load in FLASH, run in RAM
 *   .bss         → RAM                   (zeroed at boot)
 *   stack         → top of RAM            (grows down)
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH =  96K
}

/* End of stack — grows down from the top of RAM. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Reserve RAM for the main stack (rest is heap if newlib asks).
 * speexdsp uses VAR_ARRAYS=1 → VLAs in mdf.c / preprocess.c init,
 * and we observed MSP corruption with 8 KB.  16 KB has held; revisit
 * with USE_ALLOCA + a small dedicated alloca region if we ever need
 * the RAM back. */
_min_stack_size = 0x4000;  /* 16 KB */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP (*(.init))
        KEEP (*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* ARM exception unwinding tables — required by some libgcc routines. */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    /* .data: initialized RAM variables.  LMA in flash, VMA in RAM. */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Heap starts at end of .bss and grows upward.  Stack starts at
     * _estack (top of RAM) and grows downward.  _sbrk() in the runtime
     * checks they don't collide using _heap_start and (_estack -
     * _min_stack_size) as the heap upper bound.  newlib references
     * `end` for malloc's brk-based allocator. */
    . = ALIGN(8);
    _end = .;
    _heap_start = .;
    PROVIDE(end = .);

    /* Sanity check: bail at link time if even the minimum stack and
     * heap won't fit.  Doesn't reserve the heap; just verifies headroom. */
    ._stack_check :
    {
        . = ALIGN(8);
        . = . + _min_stack_size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        *(.note.*)
    }
}
