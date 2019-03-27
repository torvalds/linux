# $FreeBSD$
#
# The include file <bsd.info.mk> handles installing GNU (tech)info files.
# Texinfo is a documentation system that uses a single source
# file to produce both on-line information and printed output.
#
#
# +++ variables +++
#
# CLEANFILES	Additional files to remove for the clean and cleandir targets.
#
# DESTDIR	Change the tree where the info files gets installed. [not set]
#
# DVIPS		A program which convert a TeX DVI file to PostScript [dvips]
#
# DVIPS2ASCII	A program to convert a PostScript file which was prior
#		converted from a TeX DVI file to ascii/latin1 [dvips2ascii]
#
# FORMATS 	Indicates which output formats will be generated
#               (info, dvi, latin1, ps, html).  [info]
#
# ICOMPRESS_CMD	Program to compress info files. Output is to
#		stdout. [${COMPRESS_CMD}]
#
# INFO		texinfo files, without suffix.  [set in Makefile]
#
# INFO2HTML	A program for converting GNU info files into HTML files
#		[info2html]
#
# INFODIR	Base path for GNU's hypertext system
#		called Info (see info(1)). [${SHAREDIR}/info]
#
# INFODIRFILE	Top level node/index for info files. [dir]
#
# INFOGRP	Info group. [${SHAREGRP}]
#
# INFOMODE	Info mode. [${NOBINMODE}]
#
# INFOOWN	Info owner. [${SHAREOWN}]
#
# INFOSECTION	Default section (if one could not be found in
#		the Info file). [Miscellaneous]
#
# INSTALLINFO	A program for installing directory entries from Info
#		file in the ${INFODIR}/${INFODIRFILE}. [install-info]
#
# INSTALLINFOFLAGS	Options for ${INSTALLINFO} command. [--quiet]
#
# INSTALLINFODIRS	???
#
# MAKEINFO	A program for converting GNU Texinfo files into Info
#		file. [makeinfo]
#
# MAKEINFOFLAGS		Options for ${MAKEINFO} command. [--no-split]
#
# NO_INFOCOMPRESS	If you do not want info files be
#			compressed when they are installed. [not set]
#
# TEX		A program for converting tex files into dvi files [tex]
#
#
# +++ targets +++
#
#	install:
#		Install the info files.
#
#
# bsd.obj.mk: cleandir and obj

.include <bsd.init.mk>

MAKEINFO?=	makeinfo
MAKEINFOFLAGS+=	--no-split # simplify some things, e.g., compression
SRCDIR?=	${.CURDIR}
INFODIRFILE?=   dir
INSTALLINFO?=   install-info
INSTALLINFOFLAGS+=--quiet
INFOSECTION?=   Miscellaneous
ICOMPRESS_CMD?=	${COMPRESS_CMD}
ICOMPRESS_EXT?=	${COMPRESS_EXT}
FORMATS?=	info
INFO2HTML?=	info2html
TEX?=		tex
DVIPS?=		dvips
DVIPS2ASCII?=	dvips2ascii

.SUFFIXES: ${ICOMPRESS_EXT} .info .texi .texinfo .dvi .ps .latin1 .html

.texi.info .texinfo.info:
	${MAKEINFO} ${MAKEINFOFLAGS} -I ${.CURDIR} -I ${SRCDIR} ${.IMPSRC} \
		-o ${.TARGET}

.texi.dvi .texinfo.dvi:
	TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC} </dev/null
# Run again to resolve cross references.
	TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC} </dev/null

.texinfo.latin1 .texi.latin1:
	perl -npe 's/(^\s*\\input\s+texinfo\s+)/$$1\n@tex\n\\global\\hsize=120mm\n@end tex\n\n/' ${.IMPSRC} >> ${.IMPSRC:T:R}-la.texi
	TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC:T:R}-la.texi </dev/null
# Run again to resolve cross references.
	TEXINPUTS=${.CURDIR}:${SRCDIR}:$$TEXINPUTS \
		${TEX} ${.IMPSRC:T:R}-la.texi </dev/null
	${DVIPS} -o /dev/stdout ${.IMPSRC:T:R}-la.dvi | \
		${DVIPS2ASCII} > ${.TARGET}.new
	mv -f ${.TARGET}.new ${.TARGET}

.dvi.ps:
	${DVIPS} -o ${.TARGET} ${.IMPSRC}

.info.html:
	${INFO2HTML} ${.IMPSRC}
	${INSTALL_LINK} ${.TARGET:R}.info.Top.html ${.TARGET}

.PATH: ${.CURDIR} ${SRCDIR}

.for _f in ${FORMATS}
IFILENS+=	${INFO:S/$/.${_f}/}
.endfor

CLEANFILES+=	${IFILENS}
.if !defined(NO_INFOCOMPRESS)
CLEANFILES+=	${IFILENS:S/$/${ICOMPRESS_EXT}/}
IFILES=	${IFILENS:S/$/${ICOMPRESS_EXT}/:S/.html${ICOMPRESS_EXT}/.html/}
.else
IFILES=	${IFILENS}
.endif
.if !defined(_SKIP_BUILD)
all: ${IFILES}
.endif

.for x in ${IFILENS}
${x:S/$/${ICOMPRESS_EXT}/}:	${x}
	${ICOMPRESS_CMD} ${.ALLSRC} > ${.TARGET}
.endfor

.for x in ${INFO}
INSTALLINFODIRS+= ${x:S/$/-install/}
${x:S/$/-install/}:
.if !empty(.MAKEFLAGS:M-j)
	lockf -k ${DESTDIR}${INFODIR}/${INFODIRFILE} \
	${INSTALLINFO} ${INSTALLINFOFLAGS} \
	    --defsection=${INFOSECTION} \
	    --defentry=${INFOENTRY_${x}} \
	    ${x}.info ${DESTDIR}${INFODIR}/${INFODIRFILE}
.else
	${INSTALLINFO} ${INSTALLINFOFLAGS} \
	    --defsection=${INFOSECTION} \
	    --defentry=${INFOENTRY_${x}} \
	    ${x}.info ${DESTDIR}${INFODIR}/${INFODIRFILE}
.endif
.endfor

.PHONY: ${INSTALLINFODIRS}

.if defined(SRCS)
CLEANFILES+=	${INFO}.texi
${INFO}.texi: ${SRCS}
	cat ${.ALLSRC} > ${.TARGET}
.endif

# tex garbage
.if !empty(FORMATS:Mps) || !empty(FORMATS:Mdvi) || !empty(FORMATS:Mlatin1)
.for _f in aux cp fn ky log out pg toc tp vr dvi
CLEANFILES+=	${INFO:S/$/.${_f}/} ${INFO:S/$/-la.${_f}/}
.endfor
CLEANFILES+=	${INFO:S/$/-la.texi/}
.endif

.if !empty(FORMATS:Mhtml)
CLEANFILES+=	${INFO:S/$/.info.*.html/} ${INFO:S/$/.info/}
.endif

.if defined(INFO)
install: ${INSTALLINFODIRS}
.if !empty(IFILES:N*.html)
	${INSTALL} -o ${INFOOWN} -g ${INFOGRP} -m ${INFOMODE} \
		${IFILES:N*.html} ${DESTDIR}${INFODIR}/
.endif
.if !empty(FORMATS:Mhtml)
	${INSTALL} -o ${INFOOWN} -g ${INFOGRP} -m ${INFOMODE} \
		${INFO:S/$/.info.*.html/} ${DESTDIR}${INFODIR}/
.endif
.else
# The indirection in the following is to avoid the null install rule
# "install:" from being overridden by the implicit .sh rule if there
# happens to be a source file named install.sh.  This assumes that there
# is no source file named __null_install.sh.
install: __null_install
__null_install:
.endif

.include <bsd.obj.mk>
