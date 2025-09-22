#	$OpenBSD: Makefile,v 1.1 2003/02/17 01:29:20 henric Exp $

DEP=	busop.awk busop_h.in busop_c.in
OBJS=	busop.h busop.c

${OBJS}: ${DEP}
	awk -f busop.awk < busop_h.in > busop.h
	awk -f busop.awk < busop_c.in > busop.c

