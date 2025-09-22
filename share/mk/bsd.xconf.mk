# $OpenBSD: bsd.xconf.mk,v 1.1 2008/03/25 23:35:56 matthieu Exp $
.include <bsd.own.mk>
X11BASE?=	/usr/X11R6
X11ETC?=	/etc/X11
.if exists(${X11BASE}/share/mk/bsd.xconf.mk)
. include	"${X11BASE}/share/mk/bsd.xconf.mk"
.endif
