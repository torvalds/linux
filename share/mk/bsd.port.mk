# $FreeBSD$

.if !defined(PORTSDIR)
# Autodetect if the command is being run in a ports tree that's not rooted
# in the default /usr/ports.  The ../../.. case is in case ports ever grows
# a third level.
.for RELPATH in . .. ../.. ../../..
.if !defined(_PORTSDIR) && exists(${.CURDIR}/${RELPATH}/Mk/bsd.port.mk)
_PORTSDIR=	${.CURDIR}/${RELPATH}
.endif
.endfor
_PORTSDIR?=	/usr/ports
.if defined(.PARSEDIR)
PORTSDIR=	${_PORTSDIR:tA}
.else # fmake doesn't have :tA
PORTSDIR!=	realpath ${_PORTSDIR}
.endif
.endif

BSDPORTMK?=	${PORTSDIR}/Mk/bsd.port.mk

# Needed to keep bsd.own.mk from reading in /etc/src.conf
# and setting MK_* variables when building ports.
_WITHOUT_SRCCONF=

# Enable CTF conversion on request.
.if defined(WITH_CTF)
.undef NO_CTF
.endif

.include <bsd.own.mk>
.include "${BSDPORTMK}"
