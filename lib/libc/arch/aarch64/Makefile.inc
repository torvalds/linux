# $OpenBSD: Makefile.inc,v 1.5 2023/03/31 18:49:43 kettenis Exp $
# $NetBSD: Makefile.inc,v 1.5 2002/07/10 04:29:06 thorpej Exp $

.include <bsd.own.mk>

AFLAGS+= -mmark-bti-property

# Suppress DWARF2 warnings
DEBUG?= -gdwarf-4

CERROR=	cerror.S
