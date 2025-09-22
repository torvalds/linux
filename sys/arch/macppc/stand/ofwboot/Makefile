#	$OpenBSD: Makefile,v 1.22 2024/07/11 07:40:03 deraadt Exp $
#	$NetBSD: Makefile,v 1.2 1997/04/17 07:46:24 thorpej Exp $

S=	${.CURDIR}/../../../..
R=../

PROG=		ofwboot
CFLAGS+=	-D_STANDALONE
SRCS=		Locore.c main.c ofdev.c net.c netif_of.c netudp.c
SRCS+=		alloc.c cache.c fchmod.c hfs.c arc4.c
SRCS+=		boot.c conf.c cmd.c vars.c ctime.c strtol.c
.PATH:		${S}/arch/macppc/stand
SRCS+=		ofwmagic.S
#CFLAGS+=	-DDEBUG -DNETIF_DEBUG
CFLAGS+=	-fno-pie
NOMAN=
STRIPFLAG=
BINMODE=	444
OBJCOPY?=	objcopy
SAREL=
BINDIR=         /usr/mdec

.PATH:          ${S}/stand/boot
.PATH:          ${S}/lib/libsa
SRCS+=		hexdump.c strtoll.c ufs2.c

.PATH:		${S}/lib/libkern/arch/powerpc ${S}/lib/libkern
SRCS+=		getchar.c strlcpy.c memcpy.c memmove.c strlen.c strrchr.c \
		strlcat.c ashrdi3.c
SRCS+=		moddi3.c umoddi3.c

NEWVERSWHAT=	"OpenFirmware Boot"

# Must match . in ld.script
RELOC=		20000

CPPFLAGS+=	-I. -I${.CURDIR}/../../.. -I${.CURDIR}/../../../..
CPPFLAGS+=	-DRELOC=0x${RELOC} -DCONSPEED=57600

LIBS!=          cd $(.CURDIR)/$(R); $(MAKE) libdep

${PROG}: ${OBJS} ${LIBSA} ${LIBZ} ld.script
	${LD} -nopie -znorelro -N -X -T ${.CURDIR}/ld.script -o ${PROG} \
	    ${OBJS} ${LIBS}

.include <bsd.prog.mk>
