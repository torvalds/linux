/*
 * 
 * $FreeBSD$
 */

#include <machine/asm.h>

ENTRY(host_read)
	li %r0, 3 # SYS_read
	sc
	bso 1f
	blr
1:
	li %r3, 0
	blr

ENTRY(host_write)
	li %r0, 4 # SYS_write
	sc
	blr

ENTRY(host_seek)
	mr %r4,%r5
	mr %r5,%r6
	mr %r6,%r7
	li %r0, 140 # SYS_llseek
	sc
	blr

ENTRY(host_llseek)
	li %r0, 140 # SYS_llseek
	sc
	blr

ENTRY(host_open)
	li %r0, 5 # SYS_open
	sc
	bso 1f
	blr
1:
	li %r3, 0
	blr

ENTRY(host_close)
	li %r0, 6 # SYS_close
	sc
	blr

ENTRY(host_mmap)
	li %r0, 90 # SYS_mmap
	sc
	blr

ENTRY(host_uname)
	li %r0, 122 # SYS_uname
	sc
	blr

ENTRY(host_gettimeofday)
	li %r0, 78 # SYS_gettimeofday
	sc
	blr

ENTRY(host_select)
	li %r0, 142 # SYS_select
	sc
	blr

ENTRY(kexec_load)
	lis	%r6,21	# KEXEC_ARCH_PPC64
	li	%r0,268 # __NR_kexec_load
	sc
	blr

ENTRY(host_reboot)
	li %r0,88 # SYS_reboot
	sc
	blr

ENTRY(host_getdents)
	li %r0,141 # SYS_getdents
	sc
	blr

