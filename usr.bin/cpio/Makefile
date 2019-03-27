# $FreeBSD$

.include <src.opts.mk>

_LIBARCHIVEDIR=	${SRCTOP}/contrib/libarchive
_LIBARCHIVECONFDIR=	${SRCTOP}/lib/libarchive

PROG=	bsdcpio
BSDCPIO_VERSION_STRING=	3.3.3

.PATH:	${_LIBARCHIVEDIR}/cpio
SRCS=	cpio.c cmdline.c

.PATH:	${_LIBARCHIVEDIR}/libarchive_fe
SRCS+=	err.c line_reader.c passphrase.c

CFLAGS+= -DBSDCPIO_VERSION_STRING=\"${BSDCPIO_VERSION_STRING}\"
CFLAGS+= -DPLATFORM_CONFIG_H=\"${_LIBARCHIVECONFDIR}/config_freebsd.h\"
CFLAGS+= -I${_LIBARCHIVEDIR}/cpio -I${_LIBARCHIVEDIR}/libarchive_fe

.ifdef RELEASE_CRUNCH
# FreeBSD's installer uses cpio in crunched binaries that are
# statically linked, cannot use -lcrypto, and are size sensitive.
CFLAGS+= -DSMALLER
.endif
LIBADD=	archive

.if ${MK_ICONV} != "no"
CFLAGS+=	-DHAVE_ICONV=1 -DHAVE_ICONV_H=1 -DICONV_CONST=const
.endif

SYMLINKS=bsdcpio ${BINDIR}/cpio
MLINKS= bsdcpio.1 cpio.1

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
