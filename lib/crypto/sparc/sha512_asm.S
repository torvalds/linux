/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/opcodes.h>
#include <asm/visasm.h>

ENTRY(sha512_sparc64_transform)
	/* %o0 = digest, %o1 = data, %o2 = rounds */
	VISEntry
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	andcc	%o1, 0x7, %g0
	ldd	[%o0 + 0x30], %f12
	bne,pn	%xcc, 10f
	 ldd	[%o0 + 0x38], %f14

1:
	ldd	[%o1 + 0x00], %f16
	ldd	[%o1 + 0x08], %f18
	ldd	[%o1 + 0x10], %f20
	ldd	[%o1 + 0x18], %f22
	ldd	[%o1 + 0x20], %f24
	ldd	[%o1 + 0x28], %f26
	ldd	[%o1 + 0x30], %f28
	ldd	[%o1 + 0x38], %f30
	ldd	[%o1 + 0x40], %f32
	ldd	[%o1 + 0x48], %f34
	ldd	[%o1 + 0x50], %f36
	ldd	[%o1 + 0x58], %f38
	ldd	[%o1 + 0x60], %f40
	ldd	[%o1 + 0x68], %f42
	ldd	[%o1 + 0x70], %f44
	ldd	[%o1 + 0x78], %f46

	SHA512

	subcc	%o2, 1, %o2
	bne,pt	%xcc, 1b
	 add	%o1, 0x80, %o1

5:
	std	%f0, [%o0 + 0x00]
	std	%f2, [%o0 + 0x08]
	std	%f4, [%o0 + 0x10]
	std	%f6, [%o0 + 0x18]
	std	%f8, [%o0 + 0x20]
	std	%f10, [%o0 + 0x28]
	std	%f12, [%o0 + 0x30]
	std	%f14, [%o0 + 0x38]
	retl
	 VISExit
10:
	alignaddr %o1, %g0, %o1

	ldd	[%o1 + 0x00], %f18
1:
	ldd	[%o1 + 0x08], %f20
	ldd	[%o1 + 0x10], %f22
	ldd	[%o1 + 0x18], %f24
	ldd	[%o1 + 0x20], %f26
	ldd	[%o1 + 0x28], %f28
	ldd	[%o1 + 0x30], %f30
	ldd	[%o1 + 0x38], %f32
	ldd	[%o1 + 0x40], %f34
	ldd	[%o1 + 0x48], %f36
	ldd	[%o1 + 0x50], %f38
	ldd	[%o1 + 0x58], %f40
	ldd	[%o1 + 0x60], %f42
	ldd	[%o1 + 0x68], %f44
	ldd	[%o1 + 0x70], %f46
	ldd	[%o1 + 0x78], %f48
	ldd	[%o1 + 0x80], %f50

	faligndata %f18, %f20, %f16
	faligndata %f20, %f22, %f18
	faligndata %f22, %f24, %f20
	faligndata %f24, %f26, %f22
	faligndata %f26, %f28, %f24
	faligndata %f28, %f30, %f26
	faligndata %f30, %f32, %f28
	faligndata %f32, %f34, %f30
	faligndata %f34, %f36, %f32
	faligndata %f36, %f38, %f34
	faligndata %f38, %f40, %f36
	faligndata %f40, %f42, %f38
	faligndata %f42, %f44, %f40
	faligndata %f44, %f46, %f42
	faligndata %f46, %f48, %f44
	faligndata %f48, %f50, %f46

	SHA512

	subcc	%o2, 1, %o2
	fsrc2	%f50, %f18
	bne,pt	%xcc, 1b
	 add	%o1, 0x80, %o1

	ba,a,pt	%xcc, 5b
ENDPROC(sha512_sparc64_transform)
