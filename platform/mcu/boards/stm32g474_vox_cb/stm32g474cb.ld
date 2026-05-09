/*
 * stm32g474cb.ld — linker script for STM32G474CBT3 (custom VOX board).
 *
 * 128 KB flash @ 0x08000000
 *  32 KB SRAM (main); CCM-SRAM at 0x10000000 (10 KB) stays free for later use.
 *
 * Mirrors the Nucleo linker script except for the smaller flash/RAM
 * sizes that match the LQFP48 part.  The same startup file
 * (core/startup_stm32g474.c) and section layout work on both boards.
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH =  32K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_min_stack_size = 0x4000;  /* 16 KB — see Nucleo linker script for rationale */

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

    /* Heap from end of .bss to _estack - _min_stack_size; see _sbrk in
     * platform/mcu/core/startup_stm32g474.c. */
    . = ALIGN(8);
    _end = .;
    _heap_start = .;
    PROVIDE(end = .);

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
