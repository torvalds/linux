OUTPUT_FORMAT(binary)
SECTIONS
{
        .start : { *(.start) }
        .text : { *(.text*) }
        .rodata : { *(.rodata) }
        .data : { *(.data) }
} 