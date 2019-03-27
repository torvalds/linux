# $FreeBSD$

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/opcodes

LIB=	opcodes
SRCS+=	dis-buf.c dis-init.c disassemble.c i386-opc.c
CFLAGS+= -D_GNU_SOURCE
CFLAGS+= -I${SRCDIR}/opcodes -I${SRCDIR}/bfd
INTERNALLIB=

# If set, BINUTILSDISTDIR is the path to a directory containing the full GNU
# binutils release. FreeBSD only distributes the bits that are required to
# build native architectures. BINUTILSDISTDIR is needed to build cross tools.
.if	defined(BINUTILSDISTDIR) && exists(${BINUTILSDISTDIR})
.PATH: ${BINUTILSDISTDIR}/bfd ${BINUTILSDISTDIR}/opcodes
CFLAGS+= -I${BINUTILSDISTDIR}/opcodes -I${BINUTILSDISTDIR}/bfd
CFLAGS+= -I${BINUTILSDISTDIR}/include
.endif

.include <bsd.lib.mk>
