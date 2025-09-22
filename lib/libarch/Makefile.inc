#	$OpenBSD: Makefile.inc,v 1.4 2012/08/02 13:38:38 okan Exp $
#	$NetBSD: Makefile.inc,v 1.1 1996/02/21 02:43:57 jtk Exp $
#
# This Makefile includes boiler-plate stuff included by each subdir's Makefile.

OBJS+= ${ASM}
POBJS+=	${ASM:.o=.po}
CLEANFILES+= ${ASM} ${POBJS}

LIB=    ${MACHINE_ARCH}
