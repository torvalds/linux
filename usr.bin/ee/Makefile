# $FreeBSD$

.PATH:	${SRCTOP}/contrib/ee

CFLAGS+= -DHAS_NCURSES -DHAS_UNISTD -DHAS_STDARG -DHAS_STDLIB \
	 -DHAS_SYS_WAIT

PACKAGE=ee
PROG=	ee
LINKS=	${BINDIR}/ee ${BINDIR}/ree ${BINDIR}/ee ${BINDIR}/edit
MLINKS=	ee.1 ree.1 ee.1 edit.1
LIBADD=	ncursesw

WARNS?=	2

NLS=	C fr_FR.ISO8859-1 de_DE.ISO8859-1 pl_PL.ISO8859-2 \
	uk_UA.KOI8-U pt_BR.ISO8859-1 ru_RU.KOI8-R hu_HU.ISO8859-2

NLSLINKS_C= en_US.ISO8859-1 en_US.ISO8859-15
NLSLINKS_fr_FR.ISO8859-1= fr_BE.ISO8859-1 fr_BE.ISO8859-15 \
        fr_CA.ISO8859-1 fr_CA.ISO8859-15 fr_CH.ISO8859-1 fr_CH.ISO8859-15 \
	fr_FR.ISO8859-15
NLSLINKS_de_DE.ISO8859-1= de_AT.ISO8859-1 de_AT.ISO8859-15 de_CH.ISO8859-1 \
        de_CH.ISO8859-15 de_DE.ISO8859-15
NLSLINKS_pt_BR.ISO8859-1= pt_PT.ISO8859-1

NLSSRCFILES=ee.msg
.for lang in ${NLS}
. if exists(${.CURDIR}/nls/${lang}/ee.msg)
NLSSRCDIR_${lang}= ${.CURDIR}/nls/${lang}
. else
NLSSRCDIR_${lang}= ${SRCTOP}/contrib/ee
. endif
.endfor

.include <bsd.prog.mk>
