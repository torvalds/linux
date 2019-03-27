#
# $FreeBSD$
#

PACKAGE=lib${LIB}
OPENBSMDIR=		${SRCTOP}/contrib/openbsm
_LIBAUDITDDIR=		${OPENBSMDIR}/libauditd
_LIBBSMDIR=		${OPENBSMDIR}/libbsm

LIB=		auditd

.PATH:		${_LIBAUDITDDIR}

SRCS=	auditd_lib.c

#
# Must use BSM include files from within the contrib area, not the system.
#
CFLAGS+=	-I${OPENBSMDIR} -I${_LIBBSMDIR}

WARNS?=		3

MAN=

.include <bsd.lib.mk>

# Disable -Wcast-align.  Casting res->ai_addr in auditd_set_host triggers this
# warning, but it's ok because res->ai_addr must've originally pointed to a
# sockaddr_in or sockaddr_in6 anyway.
# Better would be to disable this warning in just that one function, but GCC
# 4.2 can't do that :( .
CWARNFLAGS.auditd_lib.c+=	-Wno-cast-align
