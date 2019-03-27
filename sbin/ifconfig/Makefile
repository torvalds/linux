#	From: @(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD$

.include <src.opts.mk>

PACKAGE=runtime
PROG=	ifconfig

SRCS=	ifconfig.c		# base support

#
# NB: The order here defines the order in which the constructors
#     are called.  This in turn defines the default order in which
#     status is displayed.  Probably should add a priority mechanism
#     to the registration process so we don't depend on this aspect
#     of the toolchain.
#
SRCS+=	af_link.c		# LLC support
.if ${MK_INET_SUPPORT} != "no"
SRCS+=	af_inet.c		# IPv4 support
.endif
.if ${MK_INET6_SUPPORT} != "no"
SRCS+=	af_inet6.c		# IPv6 support
.endif
.if ${MK_INET6_SUPPORT} != "no"
SRCS+=	af_nd6.c		# ND6 support
.endif

SRCS+=	ifclone.c		# clone device support
SRCS+=	ifmac.c			# MAC support
SRCS+=	ifmedia.c		# SIOC[GS]IFMEDIA support
SRCS+=	iffib.c			# non-default FIB support
SRCS+=	ifvlan.c		# SIOC[GS]ETVLAN support
SRCS+=	ifvxlan.c		# VXLAN support
SRCS+=	ifgre.c			# GRE keys etc
SRCS+=	ifgif.c			# GIF reversed header workaround
SRCS+=	ifipsec.c		# IPsec VTI

SRCS+=	sfp.c			# SFP/SFP+ information
LIBADD+=	m

.if ${MK_WIRELESS_SUPPORT} != "no"
SRCS+=	ifieee80211.c		# SIOC[GS]IEEE80211 support
LIBADD+=	80211
.endif

SRCS+=	carp.c			# SIOC[GS]VH support
SRCS+=	ifgroup.c		# ...
.if ${MK_PF} != "no"
SRCS+=	ifpfsync.c		# pfsync(4) support
.endif

SRCS+=	ifbridge.c		# bridge support
SRCS+=	iflagg.c		# lagg support

.if ${MK_EXPERIMENTAL} != "no"
CFLAGS+= -DDRAFT_IETF_6MAN_IPV6ONLY_FLAG
CFLAGS+= -DEXPERIMENTAL
.endif
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif
.if ${MK_INET_SUPPORT} != "no"
CFLAGS+= -DINET
.endif
.if ${MK_JAIL} != "no" && !defined(RELEASE_CRUNCH) && !defined(RESCUE)
CFLAGS+= -DJAIL
LIBADD+=	jail
.endif

MAN=	ifconfig.8

CFLAGS+= -Wall -Wmissing-prototypes -Wcast-qual -Wwrite-strings -Wnested-externs
WARNS?=	2

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
