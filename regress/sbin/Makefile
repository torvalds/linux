#	$OpenBSD: Makefile,v 1.13 2020/12/27 17:27:50 bluhm Exp $

SUBDIR+= disklabel ifconfig iked isakmpd newfs route slaacd

.if defined(REGRESS_FULL) || make(clean) || make(cleandir) || make(obj)
SUBDIR+= ipsecctl pfctl
.endif

install:

.include <bsd.subdir.mk>
