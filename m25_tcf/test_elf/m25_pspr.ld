/* PSPR/DSPR link script for the M25-3 live demo (code=PSPR, data=DSPR) */
MEMORY
{
  psprtext (rx) : ORIGIN = 0x70100000, LENGTH = 8K
  dsprdata (rw) : ORIGIN = 0x7000B000, LENGTH = 4K
}

SECTIONS
{
  .text : { *(.text*) } > psprtext
  .rodata : { *(.rodata*) } > psprtext
  .data : { *(.data*) } > dsprdata
  .bss : { *(.bss*) *(COMMON) } > dsprdata
}
