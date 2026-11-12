ENTRY(Init)
MEMORY { RAM (xrw) : ORIGIN = 0x24000004, LENGTH = 64K }
SECTIONS
{
  .text : { . = ALIGN(4); *(.text) *(.text*) *(.rodata) *(.rodata*) . = ALIGN(4); } >RAM
  .Dev_info : { KEEP(*(.Dev_info)) } >RAM
  .data : { . = ALIGN(4); *(.data) *(.data*) . = ALIGN(4); } >RAM
  .bss  : { . = ALIGN(4); *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); } >RAM
  .diag 0x24001000 (NOLOAD) : { KEEP(*(.diag)) } >RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
