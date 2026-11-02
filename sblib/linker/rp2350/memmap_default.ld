/*
 * memmap_default.ld - Pico SDK 2.2.0 memory map (rp2350) with the KNX bus interrupt path in RAM.
 *
 * Copy of pico-sdk/src/rp2_common/pico_crt0/rp2350/memmap_default.ld (SDK 2.2.0,
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd., BSD-3-Clause). Used for RP2350 builds of
 * the sblib through PICO_LINKER_SCRIPT_PATH (cmake/rp2350/pico_sdk_setup.cmake).
 *
 * Only change: the object files listed in the EXCLUDE_FILE lists of .text and .rodata are
 * not placed in flash. The .data section collects their code and constants, so crt0 copies
 * them to RAM. These are the objects with functions reachable from the bus, timer and PIO
 * interrupts (alarmIrq, pioRxIrq, rxEdgeHandler, txEdgeIrq): no XIP access, no flash cache
 * miss in the interrupt path. The functions of these objects that do not run in an interrupt
 * are copied to RAM as well (a few KB).
 *
 * - libsblib_rp2350.a: bus state machine, Timer emulation, millis(), the bus callbacks of the
 *   BCU (CallbackBcu, BcuBase, BcuDefault, TLayer4), user RAM/EEPROM and memory access,
 *   address tables, fatalError() and pinMode() of the error path, and the objects of the BCU
 *   types, which hold the inline functions of the user EEPROM classes (e.g. addrTabSize())
 * - Pico SDK: hardware_timer (time_us_64), hardware_gpio (error path)
 * - newlib: memcpy, memcmp, memset, memmove (the SDK pattern lib_a-mem*.o does not match the
 *   member names libc_a-mem*.o of the Arm GNU toolchain 13; with nosys.specs they come from libg.a)
 *
 * @author Doumanix <doumanix@gmx.de> Copyright (c) 2026
 */

/* Based on GCC ARM embedded samples.
   Defines the following symbols for use by code:
    __exidx_start
    __exidx_end
    __etext
    __data_start__
    __preinit_array_start
    __preinit_array_end
    __init_array_start
    __init_array_end
    __fini_array_start
    __fini_array_end
    __data_end__
    __bss_start__
    __bss_end__
    __end__
    end
    __HeapLimit
    __StackLimit
    __StackTop
    __stack (== StackTop)
*/

MEMORY
{
    INCLUDE "pico_flash_region.ld"
    RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k
    SCRATCH_X(rwx) : ORIGIN = 0x20080000, LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20081000, LENGTH = 4k
}

ENTRY(_entry_point)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

    /* The bootrom will enter the image at the point indicated in your
       IMAGE_DEF, which is usually the reset handler of your vector table.

       The debugger will use the ELF entry point, which is the _entry_point
       symbol, and in our case is *different from the bootrom's entry point.*
       This is used to go back through the bootrom on debugger launches only,
       to perform the same initial flash setup that would be performed on a
       cold boot.
    */

    .text : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.embedded_block))
        __embedded_block_end = .;
        KEEP (*(.reset))
        /* TODO revisit this now memset/memcpy/float in ROM */
        /* bit of a hack right now to exclude all floating point and time critical (e.g. memset, memcpy) code from
         * FLASH ... we will include any thing excluded here in .data below by default */
        *(.init)
        *libgcc.a:cmse_nonsecure_call.o
        /* sblib: bus interrupt path in RAM (see above) */
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:
                      *hardware_timer*timer.c.obj *hardware_gpio*gpio.c.obj *libc.a:libc_a-mem*.o *libg.a:libc_a-mem*.o
                      *libsblib_rp2350.a:bus.cpp.obj *libsblib_rp2350.a:timer.cpp.obj *libsblib_rp2350.a:time.cpp.obj
                      *libsblib_rp2350.a:callback_bcu.cpp.obj *libsblib_rp2350.a:bcu_base.cpp.obj
                      *libsblib_rp2350.a:bcu_default.cpp.obj *libsblib_rp2350.a:knx_tlayer4.cpp.obj
                      *libsblib_rp2350.a:userRam.cpp.obj *libsblib_rp2350.a:userEeprom.cpp.obj
                      *libsblib_rp2350.a:memory.cpp.obj *libsblib_rp2350.a:addr_tables*.cpp.obj
                      *libsblib_rp2350.a:knx_lpdu.cpp.obj *libsblib_rp2350.a:utils.cpp.obj
                      *libsblib_rp2350.a:digital_pin.cpp.obj
                      *libsblib_rp2350.a:bcu1.cpp.obj *libsblib_rp2350.a:bcu2.cpp.obj
                      *libsblib_rp2350.a:mask070*.cpp.obj *libsblib_rp2350.a:systemb.cpp.obj) .text*)
        *(.fini)
        /* Pull all c'tors into .text */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        /* Followed by destructors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(SORT(.preinit_array.*)))
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        *(SORT(.fini_array.*))
        *(.fini_array)
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.eh_frame*)
        . = ALIGN(4);
    } > FLASH

    /* Note the boot2 section is optional, and should be discarded if there is
       no reference to it *inside* the binary, as it is not called by the
       bootrom. (The bootrom performs a simple best-effort XIP setup and
       leaves it to the binary to do anything more sophisticated.) However
       there is still a size limit of 256 bytes, to ensure the boot2 can be
       stored in boot RAM.

       Really this is a "XIP setup function" -- the name boot2 is historic and
       refers to its dual-purpose on RP2040, where it also handled vectoring
       from the bootrom into the user image.
    */

    .boot2 : {
        __boot2_start__ = .;
        *(.boot2)
        __boot2_end__ = .;
    } > FLASH

    ASSERT(__boot2_end__ - __boot2_start__ <= 256,
        "ERROR: Pico second stage bootloader must be no more than 256 bytes in size")

    .rodata : {
        /* sblib: bus interrupt path in RAM (see above) */
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:
                      *hardware_timer*timer.c.obj *hardware_gpio*gpio.c.obj *libc.a:libc_a-mem*.o *libg.a:libc_a-mem*.o
                      *libsblib_rp2350.a:bus.cpp.obj *libsblib_rp2350.a:timer.cpp.obj *libsblib_rp2350.a:time.cpp.obj
                      *libsblib_rp2350.a:callback_bcu.cpp.obj *libsblib_rp2350.a:bcu_base.cpp.obj
                      *libsblib_rp2350.a:bcu_default.cpp.obj *libsblib_rp2350.a:knx_tlayer4.cpp.obj
                      *libsblib_rp2350.a:userRam.cpp.obj *libsblib_rp2350.a:userEeprom.cpp.obj
                      *libsblib_rp2350.a:memory.cpp.obj *libsblib_rp2350.a:addr_tables*.cpp.obj
                      *libsblib_rp2350.a:knx_lpdu.cpp.obj *libsblib_rp2350.a:utils.cpp.obj
                      *libsblib_rp2350.a:digital_pin.cpp.obj
                      *libsblib_rp2350.a:bcu1.cpp.obj *libsblib_rp2350.a:bcu2.cpp.obj
                      *libsblib_rp2350.a:mask070*.cpp.obj *libsblib_rp2350.a:systemb.cpp.obj) .rodata*)
        *(.srodata*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* Machine inspectable binary information */
    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info :
    {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > FLASH
    __binary_info_end = .;
    . = ALIGN(4);

    .ram_vector_table (NOLOAD): {
        *(.ram_vector_table)
    } > RAM

    .uninitialized_data (NOLOAD): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    .data : {
        __data_start__ = .;
        *(vtable)

        *(.time_critical*)

        /* remaining .text and .rodata; i.e. stuff we exclude above because we want it in RAM */
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);

        *(.data*)
        *(.sdata*)

        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__mutex_array_start = .);
        KEEP(*(SORT(.mutex_array.*)))
        KEEP(*(.mutex_array))
        PROVIDE_HIDDEN (__mutex_array_end = .);

        *(.jcr)
        . = ALIGN(4);
    } > RAM AT> FLASH

    .tdata : {
        . = ALIGN(4);
		*(.tdata .tdata.* .gnu.linkonce.td.*)
        /* All data end */
        __tdata_end = .;
    } > RAM AT> FLASH
    PROVIDE(__data_end__ = .);

    /* __etext is (for backwards compatibility) the name of the .data init source pointer (...) */
    __etext = LOADADDR(.data);

    .tbss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        __tls_base = .;
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)

        __tls_end = .;
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __tbss_end = .;

        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        PROVIDE(__global_pointer$ = . + 2K);
        *(.sbss*)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (NOLOAD):
    {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
    } > RAM
    /* historically on GCC sbrk was growing past __HeapLimit to __StackLimit, however
       to be more compatible, we now set __HeapLimit explicitly to where the end of the heap is */
    __HeapLimit = ORIGIN(RAM) + LENGTH(RAM);

    /* Start and end symbols must be word-aligned */
    .scratch_x : {
        __scratch_x_start__ = .;
        *(.scratch_x.*)
        . = ALIGN(4);
        __scratch_x_end__ = .;
    } > SCRATCH_X AT > FLASH
    __scratch_x_source__ = LOADADDR(.scratch_x);

    .scratch_y : {
        __scratch_y_start__ = .;
        *(.scratch_y.*)
        . = ALIGN(4);
        __scratch_y_end__ = .;
    } > SCRATCH_Y AT > FLASH
    __scratch_y_source__ = LOADADDR(.scratch_y);

    /* .stack*_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later
     *
     * stack1 section may be empty/missing if platform_launch_core1 is not used */

    /* by default we put core 0 stack at the end of scratch Y, so that if core 1
     * stack is not used then all of SCRATCH_X is free.
     */
    .stack1_dummy (NOLOAD):
    {
        *(.stack1*)
    } > SCRATCH_X
    .stack_dummy (NOLOAD):
    {
        KEEP(*(.stack*))
    } > SCRATCH_Y

    .flash_end : {
        KEEP(*(.embedded_end_block*))
        PROVIDE(__flash_binary_end = .);
    } > FLASH =0xaa

    /* stack limit is poorly named, but historically is maximum heap ptr */
    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __StackOneBottom = __StackOneTop - SIZEOF(.stack1_dummy);
    __StackBottom = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* picolibc and LLVM */
    PROVIDE (__heap_start = __end__);
    PROVIDE (__heap_end = __HeapLimit);
    PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
    PROVIDE( __tls_size_align = (__tls_size + __tls_align - 1) & ~(__tls_align - 1));
    PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );

    /* llvm-libc */
    PROVIDE (_end = __end__);
    PROVIDE (__llvm_libc_heap_limit = __HeapLimit);

    /* Check if data + heap + stack exceeds RAM limit */
    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")

    ASSERT( __binary_info_header_end - __logical_binary_start <= 1024, "Binary info must be in first 1024 bytes of the binary")
    ASSERT( __embedded_block_end - __logical_binary_start <= 4096, "Embedded block must be in first 4096 bytes of the binary")

    /* todo assert on extra code */
}

