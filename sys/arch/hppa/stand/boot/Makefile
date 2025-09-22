#	$OpenBSD: Makefile,v 1.30 2023/02/23 19:48:22 miod Exp $

MAN=	boot.8
MANSUBDIR=hppa
S	=${.CURDIR}/../../../..
CLEANFILES+=	boot.gdb boot.map boot.lif

.if ${MACHINE} == "hppa"
PROG=	boot
SRCS=	srt0.S exec.c conf.c

.PATH:	${S}/stand/boot
SRCS+=	bootarg.c boot.c vars.c cmd.c 

.PATH:	${S}/lib/libsa
SRCS+=	hexdump.c strtoll.c

.PATH:	${S}/lib/libkern/arch/hppa ${S}/lib/libkern
SRCS+=	milli.S ashldi3.c ashrdi3.c memcmp.c memcpy.c memmove.c memset.c \
	moddi3.c muldi3.c qdivrem.c strcmp.c strlcpy.c strlen.c \
	strncmp.c strncpy.c divdi3.c

LD?=	ld
LDFLAGS+=-Bstatic -nostartfiles -nostdlib -N -Ttext $(LINKADDR) -nopie -znorelro
LDFLAGS+=-T ${.CURDIR}/ld.script -Map boot.map
SIZE?=	size
STRIP?=	strip
STANDIR=${.CURDIR}/..
CRTBEGIN=
CRTEND=
LINKS=	${BINDIR}/boot.lif ${BINDIR}/sdboot \
	${BINDIR}/boot.lif ${BINDIR}/stboot \
	${BINDIR}/boot.lif ${BINDIR}/xxboot

LDADD=	${LIBSA} ${LIBZ}
DPADD=	${LIBSA} ${LIBZ}

all:	boot.lif

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    boot ${DESTDIR}${BINDIR}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    boot.lif ${DESTDIR}${BINDIR}

# probably we should check for 256k limit for ISL
boot.lif:	${PROG} ${ADDBOOT}
	-@cp ${PROG} ${PROG}.gdb
	${STRIP} ${PROG}
	${MKBOOT} -v ${PROG} ${ADDBOOT} boot.lif

${PROG}: $(OBJS) $(DPADD)
	$(LD) $(LDFLAGS) -o $(PROG) $(OBJS) $(LDADD)
	@${SIZE} $(PROG)
.else
NOPROG=
.endif

.include <bsd.prog.mk>

CPPFLAGS+=${DEBUGFLAGS} -DRELOC=${LOADADDR} -DHEAP_LIMIT=${HEAP_LIMIT} \
	  -DBOOT_STTY
CFLAGS+=$(SACFLAGS)
CFLAGS+=-fno-pie
AFLAGS+=-fno-pie
