#	$OpenBSD: Makefile,v 1.18 2023/03/13 11:59:39 aoyama Exp $
#	$NetBSD: Makefile,v 1.9 2013/01/22 15:48:40 tsutsui Exp $
#	@(#)Makefile	8.2 (Berkeley) 8/15/93

MAN?=	boot.8
MANSUBDIR=luna88k

.if ${MACHINE} == "luna88k"
PROG=   boot
INSTALL_STRIP=

S= ${.CURDIR}/../../../..

CPPFLAGS+=	-nostdinc -D_STANDALONE
CPPFLAGS+=	-I${.CURDIR} -I${.OBJDIR} -I${S} -I${S}/arch

CPPFLAGS+=	-DSUPPORT_DISK
CPPFLAGS+=	-DSUPPORT_ETHERNET
CPPFLAGS+=	-DSUPPORT_BOOTP
#CPPFLAGS+=	-DBOOTP_DEBUG -DNETIF_DEBUG -DETHER_DEBUG -DNFS_DEBUG
#CPPFLAGS+=	-DRPC_DEBUG -DRARP_DEBUG -DNET_DEBUG -DDEBUG -DPARANOID

OBJCOPY?=	objcopy
TEXTADDR=	0x700000	# 7MB
LDSCRIPT=	${.CURDIR}/boot.ldscript
LINKFORMAT=	-static -N -Ttext ${TEXTADDR} -T ${LDSCRIPT} \
		--warn-common -nopie -znorelro

SRCS=	locore.S
SRCS+=	init_main.c
SRCS+=	fault.c setjmp.S
SRCS+=	devopen.c
SRCS+=	conf.c
SRCS+=	exec.c
SRCS+=	sio.c
SRCS+=	bmc.c bmd.c font.c kbd.c
SRCS+=	sc.c sd.c
SRCS+=	ufs_disksubr.c
SRCS+=	cmd_luna88k.c
SRCS+=	logo.c

# netboot support
SRCS+=	if_le.c lance.c getsecs.c
SRCS+=	dev_net.c

### find out what to use for libkern
.PATH:	${S}/lib/libkern
SRCS+=	memcpy.c memmove.c strlcat.c strlcpy.c strlen.c
SRCS+=	muldi3.c negdi2.c ashldi3.c ashrdi3.c moddi3.c

### find out what to use for libsa
.PATH:	${S}/lib/libsa
SRCS+=	arc4.c ctime.c hexdump.c strtol.c strtoll.c

### MI boot code
.PATH:	${S}/stand/boot
SRCS+=	boot.c cmd.c vars.c

SAREL=
SADST=	${.CURDIR}/${__objdir}
USE_LOADFILE=	yes
.include "${S}/lib/libsa/Makefile.inc"

LIBS=	${SALIB} ${ZLIB} ${KERNLIB}

${PROG}: ${LDSCRIPT} ${OBJS} ${LIBS}
	${LD} ${LINKFORMAT} -x -o ${PROG}.elf ${OBJS} ${LIBS}
	${OBJCOPY} -S -O a.out-m88k-openbsd --impure ${PROG}.elf ${PROG}

CLEANFILES+=	${PROG}.elf

.if !make(clean) && !make(cleandir) && !make(includes) && !make(obj)
.BEGIN:
	@([ -h machine ] || ln -s ${.CURDIR}/../../include machine)
	@([ -h m88k ] || ln -s ${.CURDIR}/../../../m88k/include m88k)
.endif

.else
NOPROG=
.endif

.include <bsd.prog.mk>
