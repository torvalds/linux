/*
 * Stub arm64 vdso linker script.
 * LINUXTODO: update along with VDSO implementation
 *
 * $FreeBSD$
 */

SECTIONS
{
	. = . + SIZEOF_HEADERS;
	.text		: { *(.text*) }
	.rodata		: { *(.rodata*) }
	.hash		: { *(.hash) }
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }
	.data		: { *(.data*) }
	.dynamic	: { *(.dynamic) }
}
