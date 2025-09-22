#	$OpenBSD: Makefile,v 1.23 2020/03/12 19:19:37 otto Exp $

.include "${.CURDIR}/../Makefile.inc"

LIB=	sa

S=${.CURDIR}/../../../..
SADIR=${.CURDIR}/..
DIR_SA=	$S/lib/libsa

NOPROFILE=noprofile
NOPIC=nopic

#AS=cat ;
#AS+=	-R
#AS+=	-a

# hppa stuff
SRCS=	machdep.c pdc.c itecons.c dev_hppa.c time.c \
	ct.c dk.c lf.c lif.c cmd_hppa.c loadfile.c arc4.c elf32.c elf64.c

# stand routines
SRCS+=	alloc.c exit.c getfile.c getln.c getchar.c globals.c \
	printf.c putchar.c strerror.c strtol.c strchr.c ctime.c snprintf.c

# io routines
SRCS+=	close.c closeall.c dev.c disklabel.c dkcksum.c fchmod.c fstat.c \
	ioctl.c lseek.c open.c read.c stat.c write.c cread.c readdir.c cons.c

# boot filesystems
SRCS+=	ufs.c ufs2.c cd9660.c

.PATH:  ${DIR_SA}

all: ${SALIB}

install:

.include <bsd.lib.mk>

CPPFLAGS+=-DHEAP_LIMIT=${HEAP_LIMIT} ${DEBUGFLAGS} -DNO_NET
CPPFLAGS+=-I${S}/stand/boot
CFLAGS+=${SACFLAGS} -D__INTERNAL_LIBSA_CREAD -DCOMPAT_UFS -fno-pie
CFLAGS+=-DCONSPEED=9600
