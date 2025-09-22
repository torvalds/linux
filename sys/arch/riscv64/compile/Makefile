#	$OpenBSD: Makefile,v 1.1 2021/04/23 02:42:16 drahn Exp $

.if	make(obj) || make(clean) || make(cleandir)
SUBDIR!=find . -type d -maxdepth 1 \! \( -name . -o -name CVS \) | cut -b3-
.endif

.include <bsd.subdir.mk>
