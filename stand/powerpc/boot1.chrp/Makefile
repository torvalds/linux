# $FreeBSD$

.include <bsd.init.mk>

PROG=           boot1.elf
NEWVERSWHAT=    "Open Firmware boot block" ${MACHINE_ARCH}
INSTALLFLAGS=   -b

FILES=		boot1.hfs
SRCS=		boot1.c ashldi3.c syncicache.c

CFLAGS+=-I${LDRSRC}
LDFLAGS=-nostdlib -static -Wl,-N

.PATH:  ${SYSDIR}/libkern ${SRCTOP}/lib/libc/powerpc/gen ${.CURDIR}

# The following inserts out objects into a template HFS 
# created by generate-hfs.sh

.include "${.CURDIR}/Makefile.hfs"

boot1.hfs: boot1.elf bootinfo.txt
	echo ${.OBJDIR}
	uudecode ${.CURDIR}/hfs.tmpl.bz2.uu
	mv hfs.tmpl.bz2 ${.TARGET}.bz2
	bzip2 -f -d ${.TARGET}.bz2
	${DD} if=boot1.elf of=${.TARGET} seek=${BOOT1_OFFSET} conv=notrunc
	${DD} if=${.CURDIR}/bootinfo.txt of=${.TARGET} seek=${BOOTINFO_OFFSET} \
	    conv=notrunc

CLEANFILES+= boot1.hfs

.include <bsd.prog.mk>

