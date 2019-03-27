#	from: @(#)bsd.doc.mk	5.3 (Berkeley) 1/2/91
# $FreeBSD$
#
# The include file <bsd.doc.mk> handles installing BSD troff documents.
#
#
# +++ variables +++
#
# DCOMPRESS_CMD	Program to compress troff documents.  Output is to stdout.
#		[${COMPRESS_CMD}]
#
# DESTDIR	Change the tree where the documents get installed.  [not set]
#
# DOC		Document name.  [paper]
#
# EXTRA		Extra files (not SRCS) that make up the document.  [not set]
#
# LPR		Printer command.  [lpr]
#
# MACROS	Macro packages used to build the document.  [not set]
#
# WITHOUT_DOCCOMPRESS If you do not want formatted troff documents to be
#		compressed when they are installed.  [not set]
#
# PRINTERDEVICE	Indicates which output formats will be generated
#		(ascii, ps, html).  [ascii]
#
# SRCDIR	Directory where source files live.  [${.CURDIR}]
#
# SRCS		List of source files.  [not set]
#
# TRFLAGS	Additional flags to groff(1).  [not set]
#
# USE_EQN	If set, preprocess with eqn(1).  [not set]
#
# USE_PIC	If set, preprocess with pic(1).  [not set]
#
# USE_REFER	If set, preprocess with refer(1).  [not set]
#
# USE_SOELIM	If set, preprocess with soelim(1).  [not set]
#
# USE_TBL	If set, preprocess with tbl(1).  [not set]
#
# VOLUME	Volume the document belongs to.  [not set]

.include <bsd.init.mk>

PRINTERDEVICE?=	ascii

BIB?=		bib
GREMLIN?=	grn
GRIND?=		vgrind -f
INDXBIB?=	indxbib
PIC?=		pic
REFER?=		refer
.for _dev in ${PRINTERDEVICE:Mascii}
ROFF.ascii?=	groff -Tascii -P-c ${TRFLAGS} -mtty-char ${MACROS} ${PAGES:C/^/-o/1}
.endfor
.for _dev in ${PRINTERDEVICE:Nascii}
ROFF.${_dev}?=	groff -T${_dev} ${TRFLAGS} ${MACROS} ${PAGES:C/^/-o/1}
.endfor
SOELIM?=	soelim
TBL?=		tbl

DOC?=		paper
LPR?=		lpr

.if defined(USE_EQN)
TRFLAGS+=	-e
.endif
.if defined(USE_PIC)
TRFLAGS+=	-p
.endif
.if defined(USE_REFER)
TRFLAGS+=	-R
.endif
.if defined(USE_SOELIM)
TRFLAGS+=	-I${.CURDIR}
.endif
.if defined(USE_TBL)
TRFLAGS+=	-t
.endif

.if defined(NO_ROOT)
.if !defined(TAGS) || ! ${TAGS:Mpackage=*}
TAGS+=		package=${PACKAGE:Uruntime}
.endif
TAG_ARGS=	-T ${TAGS:[*]:S/ /,/g}
.endif

DCOMPRESS_EXT?=	${COMPRESS_EXT}
DCOMPRESS_CMD?=	${COMPRESS_CMD}
.for _dev in ${PRINTERDEVICE:Mhtml}
DFILE.html=	${DOC}.html
.endfor
.for _dev in ${PRINTERDEVICE:Nhtml}
.if ${MK_DOCCOMPRESS} == "no"
DFILE.${_dev}=	${DOC}.${_dev}
.else
DFILE.${_dev}=	${DOC}.${_dev}${DCOMPRESS_EXT}
.endif
.endfor

UNROFF?=	unroff
HTML_SPLIT?=	yes
UNROFFFLAGS?=	-fhtml
.if ${HTML_SPLIT} == "yes"
UNROFFFLAGS+=	split=1
.endif

# Compatibility mode flag for groff.  Use this when formatting documents with
# Berkeley me macros (orig_me(7)).
COMPAT?=	-C

.PATH: ${.CURDIR} ${SRCDIR}

.if !defined(_SKIP_BUILD)
.for _dev in ${PRINTERDEVICE}
all: ${DFILE.${_dev}}
.endfor
.endif

.if !target(print)
.for _dev in ${PRINTERDEVICE}
print: ${DFILE.${_dev}}
.endfor
print:
.for _dev in ${PRINTERDEVICE}
.if ${MK_DOCCOMPRESS} == "no"
	${LPR} ${DFILE.${_dev}}
.else
	${DCOMPRESS_CMD} -d ${DFILE.${_dev}} | ${LPR}
.endif
.endfor
.endif

.for _dev in ${PRINTERDEVICE:Nascii:Nps:Nhtml}
CLEANFILES+=	${DOC}.${_dev} ${DOC}.${_dev}${DCOMPRESS_EXT}
.endfor
CLEANFILES+=	${DOC}.ascii ${DOC}.ascii${DCOMPRESS_EXT} \
		${DOC}.ps ${DOC}.ps${DCOMPRESS_EXT} \
		${DOC}.html ${DOC}-*.html

realinstall:
.if ${PRINTERDEVICE:Mhtml}
	cd ${SRCDIR}; \
	${INSTALL} ${TAG_ARGS:D${TAG_ARGS},docs} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${DOC}*.html ${DESTDIR}${BINDIR}/${VOLUME}/
.endif
.for _dev in ${PRINTERDEVICE:Nhtml}
	${INSTALL} ${TAG_ARGS:D${TAG_ARGS},docs} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${DFILE.${_dev}} ${DESTDIR}${BINDIR}/${VOLUME}/
.endfor

spell: ${SRCS}
	(cd ${.CURDIR}; spell ${SRCS} ) | sort | \
		comm -23 - ${.CURDIR}/spell.ok > ${DOC}.spell

BINDIR?=	/usr/share/doc
BINMODE=	444

SRCDIR?=	${.CURDIR}

.if defined(EXTRA) && !empty(EXTRA)
_stamp.extra: ${EXTRA}
	touch ${.TARGET}
.endif

CLEANFILES+=	_stamp.extra
.for _dev in ${PRINTERDEVICE:Nhtml}
.if !target(${DFILE.${_dev}})
.if target(_stamp.extra)
${DFILE.${_dev}}: _stamp.extra
.endif
${DFILE.${_dev}}: ${SRCS}
.if ${MK_DOCCOMPRESS} == "no"
	${ROFF.${_dev}} ${.ALLSRC:N_stamp.extra} > ${.TARGET}
.else
	${ROFF.${_dev}} ${.ALLSRC:N_stamp.extra} | ${DCOMPRESS_CMD} > ${.TARGET}
.endif
.endif
.endfor

.for _dev in ${PRINTERDEVICE:Mhtml}
.if !target(${DFILE.html})
.if target(_stamp.extra)
${DFILE.html}: _stamp.extra
.endif
${DFILE.html}: ${SRCS}
.if defined(MACROS) && !empty(MACROS)
	cd ${SRCDIR}; ${UNROFF} ${MACROS} ${UNROFFFLAGS} \
	    document=${DOC} ${SRCS}
.else # unroff(1) requires a macro package as an argument
	cd ${SRCDIR}; ${UNROFF} -ms ${UNROFFFLAGS} \
	    document=${DOC} ${SRCS}
.endif
.endif
.endfor

DISTRIBUTION?=	doc

.include <bsd.obj.mk>
