/* M25-4b flash loader: code runs from PSPR, data in DSPR */
ENTRY(_start)
MEMORY
{
  psprtext (rx) : ORIGIN = 0x70100000, LENGTH = 8K
  dsprdata (rw) : ORIGIN = 0x7000B000, LENGTH = 4K
}

SECTIONS
{
  .text : { *(.text*) *(.rodata*) } > psprtext
  .data : { *(.data*) } > dsprdata
  .bss : { *(.bss*) *(COMMON) } > dsprdata
}
