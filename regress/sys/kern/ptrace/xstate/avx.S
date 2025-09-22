/* $OpenBSD: avx.S,v 1.2 2025/05/22 04:34:18 bluhm Exp $ */

#include <machine/asm.h>

	.intel_syntax noprefix

/* void ymm_write(void) */
ENTRY(ymm_write)
	vmovdqu	ymm0, [rip + .Lymm0]
	vmovdqu	ymm1, [rip + .Lymm1]
	vmovdqu	ymm2, [rip + .Lymm2]
	vmovdqu	ymm3, [rip + .Lymm3]
	vmovdqu	ymm4, [rip + .Lymm4]
	vmovdqu	ymm5, [rip + .Lymm5]
	vmovdqu	ymm6, [rip + .Lymm6]
	vmovdqu	ymm7, [rip + .Lymm7]
	vmovdqu	ymm8, [rip + .Lymm8]
	vmovdqu	ymm9, [rip + .Lymm9]
	vmovdqu	ymm10, [rip + .Lymm10]
	vmovdqu	ymm11, [rip + .Lymm11]
	vmovdqu	ymm12, [rip + .Lymm12]
	vmovdqu	ymm13, [rip + .Lymm13]
	vmovdqu	ymm14, [rip + .Lymm14]
	vmovdqu	ymm15, [rip + .Lymm15]
	ret

/* void ymm_read(struct ymm[16]) */
ENTRY(ymm_read)
	vmovdqu	[rdi + 0x000], ymm0
	vmovdqu	[rdi + 0x020], ymm1
	vmovdqu	[rdi + 0x040], ymm2
	vmovdqu	[rdi + 0x060], ymm3
	vmovdqu	[rdi + 0x080], ymm4
	vmovdqu	[rdi + 0x0a0], ymm5
	vmovdqu	[rdi + 0x0c0], ymm6
	vmovdqu	[rdi + 0x0e0], ymm7
	vmovdqu	[rdi + 0x100], ymm8
	vmovdqu	[rdi + 0x120], ymm9
	vmovdqu	[rdi + 0x140], ymm10
	vmovdqu	[rdi + 0x160], ymm11
	vmovdqu	[rdi + 0x180], ymm12
	vmovdqu	[rdi + 0x1a0], ymm13
	vmovdqu	[rdi + 0x1c0], ymm14
	vmovdqu	[rdi + 0x1e0], ymm15
	ret

	.rodata
.Lymm0:
	.rept 4; .quad 0x0000000000000000; .endr
.Lymm1:
	.rept 4; .quad 0x1111111111111111; .endr
.Lymm2:
	.rept 4; .quad 0x2222222222222222; .endr
.Lymm3:
	.rept 4; .quad 0x3333333333333333; .endr
.Lymm4:
	.rept 4; .quad 0x4444444444444444; .endr
.Lymm5:
	.rept 4; .quad 0x5555555555555555; .endr
.Lymm6:
	.rept 4; .quad 0x6666666666666666; .endr
.Lymm7:
	.rept 4; .quad 0x7777777777777777; .endr
.Lymm8:
	.rept 4; .quad 0x8888888888888888; .endr
.Lymm9:
	.rept 4; .quad 0x9999999999999999; .endr
.Lymm10:
	.rept 4; .quad 0xaaaaaaaaaaaaaaaa; .endr
.Lymm11:
	.rept 4; .quad 0xbbbbbbbbbbbbbbbb; .endr
.Lymm12:
	.rept 4; .quad 0xcccccccccccccccc; .endr
.Lymm13:
	.rept 4; .quad 0xdddddddddddddddd; .endr
.Lymm14:
	.rept 4; .quad 0xeeeeeeeeeeeeeeee; .endr
.Lymm15:
	.rept 4; .quad 0xffffffffffffffff; .endr
