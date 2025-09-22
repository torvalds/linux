#	$OpenBSD: Makefile,v 1.2 2016/10/15 13:45:08 deraadt Exp $

.if	make(obj) || make(clean) || make(cleandir)
SUBDIR!=find . -type d -maxdepth 1 \! \( -name . -o -name CVS \) | cut -b3-
.endif

.include <bsd.subdir.mk>
