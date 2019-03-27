#
# $FreeBSD$
#

.include <src.opts.mk>
PACKAGE=	rescue
MK_SSP=		no

# Certain library entries have hard-coded references to
# /bin, /sbin, etc, that require those entries to be
# recompiled for use in /rescue.  This Makefile
# accomplishes that.  Note that this is pure build hackery.
# This library should never be installed, and isn't even linked
# with in the normal way. (See ../rescue/Makefile for details.)

.PATH: ${SRCTOP}/lib/libc/gen \
       ${SRCTOP}/lib/libc/net \
       ${SRCTOP}/lib/libc/stdlib \
       ${SRCTOP}/lib/libutil 

LIB=		rescue
INTERNALLIB=	# Don't install this library
SRCS=		exec.c getusershell.c login_class.c popen.c rcmdsh.c \
		sysctl.c system.c

CFLAGS+=	-DRESCUE
# Flags copied from src/lib/libc and src/lib/libutil
# libc/db/Makefile.inc
CFLAGS+=	-D__DBINTERFACE_PRIVATE
# libc/net/Makefile.inc & libutil/Makefile
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=	-DINET6
.endif
# libc/regex/Makefile.inc & libc/regex/grot/Makefile
CFLAGS+=	-DPOSIX_MISTAKE
# libc/rpc/Makefile.inc
CFLAGS+=	-DBROKEN_DES -DPORTMAP -DDES_BUILTIN
# libc/Makefile
.if ${MK_NIS} != "no"
CFLAGS+=       -DYP
.endif
.if ${MK_HESIOD} != "no"
CFLAGS+=       -DHESIOD
.endif
CFLAGS+=	-I${SRCTOP}/lib/libc/include

.include <bsd.lib.mk>
