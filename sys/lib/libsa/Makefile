#	$OpenBSD: Makefile,v 1.31 2022/09/02 10:14:02 miod Exp $
#	$NetBSD: Makefile,v 1.13 1996/10/02 16:19:51 ws Exp $

LIB=	sa

DIR=${SAREL}${SADIR}

.PATH: ${DIR}

#DEBUGFLAGS= -DBOOTP_DEBUG -DNETIF_DEBUG -DETHER_DEBUG -DNFS_DEBUG -DRPC_DEBUG -DRARP_DEBUG -DDEBUG -DPARANOID -Wall
#DEBUGFLAGS= -Wall
AFLAGS+=${XAFLAGS}
CFLAGS+=${XCFLAGS} -D_STANDALONE -DCOMPAT_UFS $(DEBUGFLAGS) -I${DIR} -I${DIR}/../..
#
# Needed for alpha and macppc
CFLAGS+=$(EXTRACFLAGS)

.if defined(USE_LOADFILE)
SRCS+= loadfile.c
.endif

.if defined(SA_ZLIB)
CPPFLAGS+= -D__INTERNAL_LIBSA_CREAD
.endif

# stand routines
SRCS+=	alloc.c memcpy.c memmove.c exit.c getfile.c getchar.c getln.c globals.c \
	printf.c putchar.c snprintf.c strerror.c strcmp.c memset.c memcmp.c \
	strncpy.c strncmp.c strchr.c

# math from libkern
SRCS+=	divdi3.c qdivrem.c
.PATH:	${DIR}/../../lib/libkern

# io routines
SRCS+=	close.c closeall.c dev.c disklabel.c dkcksum.c cons.c ioctl.c \
	lseek.c open.c nullfs.c read.c stat.c fchmod.c fstat.c \
	write.c readdir.c

# boot filesystems
SRCS+=	ufs.c ufs2.c cd9660.c

.if defined(SA_ZLIB)
SRCS+=	cread.c
.endif

.if !defined(NO_NET)
SRCS+=	nfs.c

# network routines
SRCS+=	arp.c ether.c in_cksum.c net.c netif.c netudp.c rpc.c

# network info services:
SRCS+=	bootp.c rarp.c bootparam.c

.else
# C flags
CFLAGS+=-DNO_NET
.endif


NOPROFILE=
NOPIC=
OBJMACHINE=

install:

.include <bsd.lib.mk>
