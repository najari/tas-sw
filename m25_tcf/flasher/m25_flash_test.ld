/* M25-4b2 test: link the test program into the LAST PFLASH sectors (currently erased/zeros) */
ENTRY(_start)
MEMORY
{
  pflash (rx) : ORIGIN = 0x805F0000, LENGTH = 16K
  dspr   (rw) : ORIGIN = 0x70000000, LENGTH = 8K
}

SECTIONS
{
  .text : { *(.text*) *(.rodata*) } > pflash
  .data : { *(.data*) } > dspr
  .bss : { *(.bss*) *(COMMON) } > dspr
}
