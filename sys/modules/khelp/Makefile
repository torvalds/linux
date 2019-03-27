# $FreeBSD$

SUBDIR=	

# Do we have the TCP_HHOOK symbol defined? If not, there is no point in
# building this modules by default.
# We will default to building this module unless $OPT_INET is defined
# and does not contain the TCP_HHOOK option.
.if defined(ALL_MODULES) || ${OPT_INET:UTCP_HHOOK:MTCP_HHOOK} != ""
SUBDIR+=	h_ertt
.endif

.include <bsd.subdir.mk>
