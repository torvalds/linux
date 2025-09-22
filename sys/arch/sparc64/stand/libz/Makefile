#	$OpenBSD: Makefile,v 1.10 2024/01/28 01:07:26 deraadt Exp $

S=${.CURDIR}/../../../..
ZDST=${.OBJDIR}

.PATH: ${S}/lib/libz

.include "${S}/lib/libz/Makefile"
CFLAGS+=${CEXTRAFLAGS} ${AFLAGS} -I${S} -fno-pie -fno-stack-protector
CPPFLAGS+=-DDYNAMIC_CRC_TABLE -DBUILDFIXED
