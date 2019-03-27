# $FreeBSD$
# Makefile for file(1) cmd.
# Copyright (c) David E. O'Brien, 2000-2004, 2009
# Copyright (c) Ian F. Darwin 86/09/01 - see LEGAL.NOTICE.
#
# This software is not subject to any license of the American Telephone
# and Telegraph Company or of the Regents of the University of California.
#
# Permission is granted to anyone to use this software for any purpose on
# any computer system, and to alter it and redistribute it freely, subject
# to the following restrictions:
#
# 1. The author is not responsible for the consequences of use of this
#    software, no matter how awful, even if they arise from flaws in it.
# 2. The origin of this software must not be misrepresented, either by
#    explicit claim or by omission.  Since few users ever read sources,
#    credits must appear in the documentation.
# 3. Altered versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.  Since few users
#    ever read sources, credits must appear in the documentation.
# 4. This notice may not be removed or altered.

SRCDIR=	${SRCTOP}/contrib/file
.PATH: ${SRCDIR}/src
.PATH: ${SRCDIR}/doc

PROG=	file

MAGICPATH?=	/usr/share/misc

CFLAGS+= -DMAGIC='"${MAGICPATH}/magic"' -DHAVE_CONFIG_H
CFLAGS+= -I${SRCTOP}/lib/libmagic

LIBADD=	magic

FILEVER!= awk '$$1 == "\#define" && $$2 == "VERSION" { print $$3; exit }' \
			${SRCTOP}/lib/libmagic/config.h

CLEANFILES+=	${MAN}

.include <bsd.prog.mk>

.for mp in ${MAN}
${mp}: ${mp:C/[0-9]/man/}
	sed -e 's/__FSECTION__/5/g' -e 's/__CSECTION__/1/g' \
		-e 's/__VERSION__/${FILEVER}/g' \
		-e 's,__MAGIC__,${MAGICPATH}/magic,g' ${.ALLSRC} > ${.TARGET}
.endfor
