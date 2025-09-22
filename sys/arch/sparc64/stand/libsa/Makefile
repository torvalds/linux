#	$OpenBSD: Makefile,v 1.16 2024/01/28 01:07:26 deraadt Exp $

LIB=	sa

.PATH:	${.CURDIR}/../../../../lib/libsa

CLEANFILES += machine

CFLAGS= ${CEXTRAFLAGS} ${AFLAGS} -O2 -D__INTERNAL_LIBSA_CREAD -nostdinc \
	-fno-pie -fno-stack-protector \
	-I${.CURDIR} -I${.CURDIR}/../include -I${.CURDIR}/../.. \
	-I${.CURDIR}/../../.. -I${.CURDIR}/../../../.. \
	-I${.CURDIR}/../../../../lib/libsa \
	-I${.OBJDIR}

CPPFLAGS+= -D__INTERNAL_LIBSA_CREAD

# stand routines
SRCS=	alloc.c exit.c getfile.c getln.c globals.c \
	memcmp.c memcpy.c memmove.c memset.c printf.c snprintf.c strerror.c strncpy.c

# io routines
SRCS+=	close.c closeall.c dev.c disklabel.c dkcksum.c fstat.c fchmod.c \
	ioctl.c lseek.c open.c read.c stat.c write.c cread.c

# network routines
SRCS+=	arp.c ether.c in_cksum.c net.c netif.c netudp.c rpc.c

# network info services
SRCS+=	bootp.c bootparam.c rarp.c

# boot filesystems
SRCS+=	ufs.c ufs2.c nfs.c cd9660.c

${OBJS}:	${.CURDIR}/../Makefile.inc

NOPROFILE=
NOPIC=

.if !make(clean) && !make(cleandir) && !make(includes) && !make(libdep) && \
    !make(sadep) && !make(salibdir) && !make(obj)
.BEGIN:
	@([ -h machine ] || ln -s ${.CURDIR}/../../include machine)
.endif

install:

.include <bsd.lib.mk>
