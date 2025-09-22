#	$OpenBSD: Makefile,v 1.1 2020/05/16 17:11:14 kettenis Exp $

.if	make(obj) || make(clean) || make(cleandir)
SUBDIR!=find . -type d -maxdepth 1 \! \( -name . -o -name CVS \) | cut -b3-
.endif

.include <bsd.subdir.mk>
