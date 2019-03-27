#	from $NetBSD: Makefile.inc,v 1.7 1995/02/27 13:06:20 cgd Exp $
# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/nls

SRCS+=	msgcat.c

SYM_MAPS+=${LIBC_SRCTOP}/nls/Symbol.map

MAN+=	catclose.3 catgets.3 catopen.3

# NOTE: C.msg should not be processed here, it's used as a template
#       for translators.

NLSNAME=	libc
NLS+=	be_BY.UTF-8
NLS+=	ca_ES.ISO8859-1
NLS+=	de_DE.ISO8859-1
NLS+=	el_GR.ISO8859-7
NLS+=	es_ES.ISO8859-1
NLS+=	fi_FI.ISO8859-1
NLS+=	fr_FR.ISO8859-1
NLS+=	gl_ES.ISO8859-1
NLS+=	hu_HU.ISO8859-2
NLS+=	it_IT.ISO8859-15
NLS+=	ja_JP.UTF-8
NLS+=	ja_JP.eucJP
NLS+=	ko_KR.UTF-8
NLS+=	ko_KR.eucKR
NLS+=	mn_MN.UTF-8
NLS+=	nl_NL.ISO8859-1
NLS+=	no_NO.ISO8859-1
NLS+=	pl_PL.ISO8859-2
NLS+=	pt_BR.ISO8859-1
NLS+=	ru_RU.KOI8-R
NLS+=	sk_SK.ISO8859-2
NLS+=	sv_SE.ISO8859-1
NLS+=	uk_UA.UTF-8
NLS+=	zh_CN.GB18030
NLS+=	zh_CN.GB2312
NLS+=	zh_CN.UTF-8
