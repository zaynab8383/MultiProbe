/*
******************************************************************************
**
**  File        : LinkerScript.ld
**
**  Author      : STM32CubeIDE
**
**  Abstract    : Linker script for STM32H7 series
**                128Kbytes FLASH and 1376Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used.
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
*****************************************************************************
** @attention
**
** Copyright (c) 2025 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);    /* end of RAM */
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;      /* required amount of heap  */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Specify the memory areas */
MEMORY
{
  ITCMRAM (xrw)  : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH (rx)     : ORIGIN = 0x08000000, LENGTH = 128K
  EXTFLASH (rx)  : ORIGIN = 0x90000000, LENGTH = 1M      /* W25Q64 first 1 MB = firmware image (code+const); logo/video use 1 MB+ */
  DTCMRAM1 (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
  DTCMRAM2 (xrw) : ORIGIN = 0x20010000, LENGTH = 64K
  RAM (xrw)      : ORIGIN = 0x24000000, LENGTH = 640K
  STAGE (rx)     : ORIGIN = 0x240A0000, LENGTH = 384K   /* debugger drops the plain firmware image here; Boot_ExternalInit() encrypts+programs it to the W25Q64 */
  RAM_CD (xrw)   : ORIGIN = 0x30000000, LENGTH = 128K
  RAM_SRD (xrw)  : ORIGIN = 0x38000000, LENGTH = 32K
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* Boot stage: runs BEFORE the external flash is mapped/decrypted, so it must stay in internal flash.
   * Also w25q64.c: it takes the external flash out of memory-mapped mode (erase/program). */
  .boot_text :
  {
    . = ALIGN(4);
    *startup_stm32h7b0vbtx.o(.text .text.*)
    *system_stm32h7xx.o(.text .text.* .rodata .rodata.*)
    *xip_boot.o(.text .text.* .rodata .rodata.*)
    *aes_boot.o(.text .text.* .rodata .rodata.*)
    *otf_crypto.o(.text .text.* .rodata .rodata.*)
    *w25q64.o(.text .text.* .rodata .rodata.*)
    . = ALIGN(4);
  } >FLASH

  /* ---- Everything below is the external-flash image -------------------------------------------------------
   * Run address (VMA) = W25Q64 mapped at 0x90000000 (encrypted by OTFDEC when OTF_ENABLE = 1).
   * Load address (LMA) = STAGE in RAM, mirroring the same offsets: the debugger drops the plain image there and
   * Boot_ExternalInit() encrypts + programs it into the W25Q64. No external loader needed.                     */
  .extdata : AT(ORIGIN(STAGE) + ADDR(.extdata) - ORIGIN(EXTFLASH))
  {
    . = ALIGN(16);
    *pages_assets.o(.rodata .rodata.*)
    *logo_image_rle.o(.rodata .rodata.*)
    . = ALIGN(4);
  } >EXTFLASH

  .text : AT(ORIGIN(STAGE) + ADDR(.text) - ORIGIN(EXTFLASH))
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >EXTFLASH

  .rodata : AT(ORIGIN(STAGE) + ADDR(.rodata) - ORIGIN(EXTFLASH))
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >EXTFLASH

  /* exidx/extab dropped: PREL31 cannot span internal flash <-> 0x90000000 */
  PROVIDE(__exidx_start = 0);
  PROVIDE(__exidx_end = 0);

  .preinit_array (READONLY) : AT(ORIGIN(STAGE) + ADDR(.preinit_array) - ORIGIN(EXTFLASH))
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >EXTFLASH

  .init_array (READONLY) : AT(ORIGIN(STAGE) + ADDR(.init_array) - ORIGIN(EXTFLASH))
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >EXTFLASH

  .fini_array (READONLY) : AT(ORIGIN(STAGE) + ADDR(.fini_array) - ORIGIN(EXTFLASH))
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(16);
    _extimg_end = .;
  } >EXTFLASH

  _extstage_base = ORIGIN(STAGE);
  _extimg_size   = _extimg_end - ORIGIN(EXTFLASH);
  ASSERT(_extimg_size < LENGTH(STAGE) - 64, "External-flash image does not fit the STAGE RAM area (enlarge STAGE)")
  ASSERT(_extimg_size < LENGTH(EXTFLASH), "External-flash image larger than 1 MB (the rest of the W25Q64 is used by logo/video)")

  /* marker: "a freshly staged image is waiting" - at the very end of STAGE, set by the debugger load */
  .extmagic (ORIGIN(STAGE) + LENGTH(STAGE) - 8) :
  {
    _extmagic = .;
    LONG(0x4D505153)
    LONG(0xA5C3965A)
  } >STAGE

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >RAM AT> FLASH

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    *(.ARM.exidx* .ARM.extab* .gnu.linkonce.armextab.*)
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
