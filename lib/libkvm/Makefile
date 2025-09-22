#	$OpenBSD: Makefile,v 1.19 2017/12/14 17:06:33 guenther Exp $
#	$NetBSD: Makefile,v 1.11 1996/03/18 22:33:07 thorpej Exp $
#	from: @(#)Makefile	8.1 (Berkeley) 6/4/93

LIB=	kvm

# Try most specific name first.
.if	exists(kvm_${MACHINE}.c)
SRCS=	kvm_${MACHINE}.c
.else
# Less specific name (i.e. m68k)
.if	exists(kvm_${MACHINE_ARCH}.c)
SRCS=	kvm_${MACHINE_ARCH}.c
.else
# Even less specific name (i.e. mips64)
CFLAGS+=-D${MACHINE_CPU}
SRCS=	kvm_${MACHINE_CPU}.c
.endif
.endif

VERSION_SCRIPT=	${.CURDIR}/Symbols.map

CFLAGS+= -D_LIBKVM
SRCS+=	kvm.c kvm_file2.c kvm_getloadavg.c kvm_proc.c kvm_proc2.c \
	kvm_cd9660.c kvm_udf.c kvm_ntfs.c

MAN=	kvm.3 kvm_dump.3 kvm_geterr.3 kvm_getfiles.3 kvm_getloadavg.3 \
	kvm_getprocs.3 kvm_nlist.3 kvm_open.3 kvm_read.3

.include <bsd.lib.mk>
