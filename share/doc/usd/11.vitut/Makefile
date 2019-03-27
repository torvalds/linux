#	From: @(#)Makefile	8.1 (Berkeley) 6/8/93
# $FreeBSD$

VOLUME=		usd/11.edit
SRCS=		edittut.ms
MACROS=		-ms
USE_TBL=

# index for versatec is different from the one in edit.tut
# because the fonts are different and entries reference page
# rather than section numbers.  if you have a typesetter
# you should just use the index in edit.tut, and ignore editvindex.

#editvindex:
#	${TROFF} ${MACROS} -n22 edit.vindex

.include <bsd.doc.mk>
