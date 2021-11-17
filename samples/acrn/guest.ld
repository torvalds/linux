/* SPDX-License-Identifier: GPL-2.0 */
OUTPUT_FORMAT(binary)
SECTIONS
{
        .start : { *(.start) }
        .text : { *(.text*) }
        .rodata : { *(.rodata) }
        .data : { *(.data) }
}
