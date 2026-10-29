OUTPUT_ARCH("riscv")
ENTRY(eapp_entry)
SECTIONS
{
    . = 0x1000;
    .text : { *(.text._start) *(.text .text.*) }
    . = ALIGN(0x1000);
    .rodata : { *(.srodata .srodata.* .rodata .rodata.*) }
    .data : { *(.sdata .sdata.* .data .data.*) }
    .bss : { *(.sbss .sbss.* .bss .bss.*) *(COMMON)
             . = ALIGN(0x1000); }
    _end = .;
}
