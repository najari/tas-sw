/* Minimal TriCore link script for the M25-3 ELF test */
MEMORY
{
  pflashtext (rx) : ORIGIN = 0x80000000, LENGTH = 1M
  dsprdata (rw)   : ORIGIN = 0x70000000, LENGTH = 64K
}

SECTIONS
{
  .text : { *(.text*) } > pflashtext
  .rodata : { *(.rodata*) } > pflashtext
  .data : { *(.data*) } > dsprdata
  .bss : { *(.bss*) *(COMMON) } > dsprdata
}
