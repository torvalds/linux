#	$OpenBSD: files.sh,v 1.7 2019/04/25 21:47:53 deraadt Exp $
#	$NetBSD: files.sh3,v 1.32 2005/12/11 12:18:58 christos Exp $

file	arch/sh/sh/cache.c
file	arch/sh/sh/cache_sh3.c		sh3
file	arch/sh/sh/cache_sh4.c		sh4
file	arch/sh/sh/clock.c
file	arch/sh/sh/db_disasm.c		ddb
file	arch/sh/sh/db_interface.c	ddb
file	arch/sh/sh/db_memrw.c		ddb
file	arch/sh/sh/db_trace.c		ddb
file	arch/sh/sh/devreg.c		sh3 & sh4
file	arch/sh/sh/interrupt.c
file	arch/sh/sh/locore_c.c
file	arch/sh/sh/locore_subr.S
file	arch/sh/sh/mem.c
file	arch/sh/sh/mmu.c
file	arch/sh/sh/mmu_sh3.c		sh3
file	arch/sh/sh/mmu_sh4.c		sh4
file	arch/sh/sh/pmap.c
file	arch/sh/sh/process_machdep.c
file	arch/sh/sh/sh_machdep.c
file	arch/sh/sh/sys_machdep.c
file	arch/sh/sh/trap.c
file	arch/sh/sh/vectors.S
file	arch/sh/sh/vm_machdep.c

file	arch/sh/sh/in_cksum.S
file	netinet/in4_cksum.c

file	dev/cninit.c

# quad support is necessary for 32 bit architectures
file	lib/libkern/adddi3.c
file	lib/libkern/anddi3.c
file	lib/libkern/ashldi3.c
file	lib/libkern/ashrdi3.c
file	lib/libkern/cmpdi2.c
file	lib/libkern/divdi3.c
file	lib/libkern/iordi3.c
file	lib/libkern/lshldi3.c
file	lib/libkern/lshrdi3.c
file	lib/libkern/moddi3.c
file	lib/libkern/muldi3.c
file	lib/libkern/negdi2.c
file	lib/libkern/notdi2.c
file	lib/libkern/qdivrem.c
file	lib/libkern/subdi3.c
file	lib/libkern/ucmpdi2.c
file	lib/libkern/udivdi3.c
file	lib/libkern/umoddi3.c
file	lib/libkern/xordi3.c

file	lib/libkern/arch/sh/movstr_i4.S
file	lib/libkern/arch/sh/movstrSI12_i4.S
file	lib/libkern/arch/sh/sdivsi3.S
file	lib/libkern/arch/sh/udivsi3.S
