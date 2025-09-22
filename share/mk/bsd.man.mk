#	$OpenBSD: bsd.man.mk,v 1.42 2017/07/21 15:18:35 espie Exp $
#	$NetBSD: bsd.man.mk,v 1.23 1996/02/10 07:49:33 jtc Exp $
#	@(#)bsd.man.mk	5.2 (Berkeley) 5/11/90

.if !target(.MAIN)
.  if exists(${.CURDIR}/../Makefile.inc)
.    include "${.CURDIR}/../Makefile.inc"
.  endif

.MAIN: all
.endif

BEFOREMAN?=

# Add / so that we don't have to specify it.
.if defined(MANSUBDIR) && !empty(MANSUBDIR)
MANSUBDIR:=${MANSUBDIR:S,^,/,:S,$,/,}
.else
MANSUBDIR=/
.endif

# Files contained in ${BEFOREMAN} must be built before generating any
# manual page source code.
.for page in ${MAN}
.  if target(${page})
${page}: ${BEFOREMAN}
.  endif
.endfor

# Install the real manuals.
.for page in ${MAN}
.  for sub in ${MANSUBDIR}
_MAN_INST=${DESTDIR}${MANDIR}${page:E}${sub}${page:T}
${_MAN_INST}: ${page}
	${INSTALL} ${INSTALL_COPY} -o ${MANOWN} -g ${MANGRP} -m ${MANMODE} \
		${.ALLSRC} ${.TARGET}

maninstall: ${_MAN_INST}

# XXX not really a phony target, but force installation each time
# maninstall is run.
.PHONY: ${_MAN_INST}
.  endfor
.endfor

# Install the manual hardlinks, if any.
maninstall:
.if defined(MLINKS) && !empty(MLINKS)
.  for sub in ${MANSUBDIR}
.     for lnk file in ${MLINKS}
	@l=${DESTDIR}${MANDIR}${lnk:E}${sub}${lnk}; \
	t=${DESTDIR}${MANDIR}${file:E}${sub}${file}; \
	echo $$t -\> $$l; \
	rm -f $$t; ln $$l $$t;
.     endfor
.  endfor
.endif

# Explicitly list ${BEFOREMAN} to get it done even if ${MAN} is empty.
all: ${BEFOREMAN} ${MAN}

manlint: ${MAN}
.if defined(MAN) && !empty(MAN)
	mandoc -Tlint ${.ALLSRC}
.endif

.PHONY: manlint
