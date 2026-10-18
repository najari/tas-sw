/* RAM (DSPR) link script for the M25-3 live demo (core0 DSPR bus alias) */
MEMORY
{
  ramtext (rx) : ORIGIN = 0x7000A000, LENGTH = 4K
  ramdata (rw) : ORIGIN = 0x7000B000, LENGTH = 4K
}

SECTIONS
{
  .text : { *(.text*) } > ramtext
  .rodata : { *(.rodata*) } > ramtext
  .data : { *(.data*) } > ramdata
  .bss : { *(.bss*) *(COMMON) } > ramdata
}
