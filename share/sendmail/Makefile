# $FreeBSD$
#
# Doing a make install builds /usr/share/sendmail/

PACKAGE=	sendmail
FILESGROUPS+=	SM
SENDMAIL_DIR=	${SRCTOP}/contrib/sendmail
CFDIR=	cf
CFPACKAGE=	sendmail
CFDIRS!=	(cd ${SENDMAIL_DIR}; find -L ${CFDIR} \( \( -name CVS -o -name .svn \) -prune \) -o -type d -print)
CFFILES!=	(cd ${SENDMAIL_DIR}; find -L ${CFDIR} \( \( -name CVS -o -name .svn \) -prune \) -o -type f -print)
SMFILES= 	${CFFILES}
SMFILESDIR=	${DDIR}

DDIR=	${DESTDIR}${SHAREDIR}/sendmail

# Define SHARED to indicate whether you want symbolic links to the system
# source (``symlinks''), or a separate copy (``copies'')
SHARED?=	copies

all clean cleandir depend lint tags:

beforeinstall: ${SHARED}
META_TARGETS+=	copies symlinks

copies:
	if [ -L ${DDIR}/${CFDIR} ]; then rm -f ${DDIR}/${CFDIR}; fi
.for dir in ${CFDIRS}
	${INSTALL} -T package=${PACKAGE:Usendmail} ${TAGS_ARGS} -o ${BINOWN} -g ${BINGRP} -m 755 -d ${DDIR}/${dir}
.endfor
.for file in ${CFFILES}
	${INSTALL} -T package=${PACKAGE:Usendmail} ${TAGS_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 ${SENDMAIL_DIR}/${file} ${DDIR}/${file}
.endfor

symlinks:
	rm -rf ${DDIR}/${CFDIR}; ln -s ${SENDMAIL_DIR}/${CFDIR} ${DDIR}/${CFDIR}

.include <bsd.prog.mk>
