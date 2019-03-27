# $FreeBSD$

SUBDIR=	cc_cubic \
	cc_dctcp \
	cc_htcp


# Do we have the TCP_HHOOK symbol defined? If not, there is no point in
# building these modules by default.
# We will default to building these modules unless $OPT_INET is defined
# and does not contain the TCP_HHOOK option.
.if defined(ALL_MODULES) || ${OPT_INET:UTCP_HHOOK:MTCP_HHOOK} != ""
SUBDIR+= \
	cc_cdg \
	cc_chd \
	cc_hd \
	cc_vegas
.endif

.include <bsd.subdir.mk>
