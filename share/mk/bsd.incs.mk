# $FreeBSD$

.if !target(__<bsd.init.mk>__)
.error bsd.incs.mk cannot be included directly.
.endif

.if ${MK_INCLUDES} != "no"

INCSGROUPS?=	INCS

.if defined(NO_ROOT)
.if !defined(TAGS) || ! ${TAGS:Mpackage=*}
TAGS+=		package=${PACKAGE:Uruntime}
.endif
TAG_ARGS=	-T ${TAGS:[*]:S/ /,/g}
.endif

.if !target(buildincludes)
.for group in ${INCSGROUPS}
buildincludes: ${${group}}
.endfor
.endif

.if !defined(_SKIP_BUILD)
all: buildincludes
.endif

.if !target(installincludes)
.for group in ${INCSGROUPS}
.if defined(${group}) && !empty(${group})

${group}OWN?=	${BINOWN}
${group}GRP?=	${BINGRP}
${group}MODE?=	${NOBINMODE}
${group}DIR?=	${INCLUDEDIR}${PRIVATELIB:D/private/${LIB}}
STAGE_SETS+=	${group:C,[/*],_,g}
STAGE_DIR.${group:C,[/*],_,g}= ${STAGE_OBJTOP}${${group}DIR}
STAGE_SYMLINKS_DIR.${group:C,[/*],_,g}= ${STAGE_OBJTOP}

_${group}INCS=
.for header in ${${group}}
.if defined(${group}OWN_${header:T}) || defined(${group}GRP_${header:T}) || \
    defined(${group}MODE_${header:T}) || defined(${group}DIR_${header:T}) || \
    defined(${group}NAME_${header:T}) || defined(${group}NAME)
${group}OWN_${header:T}?=	${${group}OWN}
${group}GRP_${header:T}?=	${${group}GRP}
${group}MODE_${header:T}?=	${${group}MODE}
${group}DIR_${header:T}?=	${${group}DIR}
.if defined(${group}NAME)
${group}NAME_${header:T}?=	${${group}NAME}
.else
${group}NAME_${header:T}?=	${header:T}
.endif
STAGE_AS_SETS+= ${header:T}
STAGE_AS_${header:T}= ${${group}NAME_${header:T}}
# XXX {group}OWN,GRP,MODE
STAGE_DIR.${header:T}= ${STAGE_OBJTOP}${${group}DIR_${header:T}}
stage_as.${header:T}: ${header}
stage_includes: stage_as.${header:T}

installincludes: _${group}INS_${header:T}
_${group}INS_${header:T}: ${header}
	${INSTALL} -C -o ${${group}OWN_${.ALLSRC:T}} \
	    -g ${${group}GRP_${.ALLSRC:T}} -m ${${group}MODE_${.ALLSRC:T}} \
	    ${.ALLSRC} \
	    ${DESTDIR}${${group}DIR_${.ALLSRC:T}}/${${group}NAME_${.ALLSRC:T}}
.else
_${group}INCS+= ${header}
.endif
.endfor
.if !empty(_${group}INCS)
stage_files.${group}: ${_${group}INCS}
stage_includes: stage_files.${group}

installincludes: _${group}INS
_${group}INS: ${_${group}INCS}
.if defined(${group}NAME)
	${INSTALL} ${TAG_ARGS:D${TAG_ARGS},development} -C -o ${${group}OWN} -g ${${group}GRP} -m ${${group}MODE} \
	    ${.ALLSRC} ${DESTDIR}${${group}DIR}/${${group}NAME}
.else
	${INSTALL} ${TAG_ARGS:D${TAG_ARGS},development} -C -o ${${group}OWN} -g ${${group}GRP} -m ${${group}MODE} \
	    ${.ALLSRC} ${DESTDIR}${${group}DIR}/
.endif
.endif

.endif # defined(${group}) && !empty(${group})
.endfor

.if defined(INCSLINKS) && !empty(INCSLINKS)
installincludes:
.for s t in ${INCSLINKS}
	${INSTALL_SYMLINK} ${TAG_ARGS:D${TAG_ARGS},development} ${s} ${DESTDIR}${t}
.endfor
.endif
.endif # !target(installincludes)

realinstall: installincludes
.ORDER: beforeinstall installincludes

.if ${MK_STAGING} != "no" && !defined(_SKIP_BUILD)
.if !defined(NO_STAGE_INCLUDES)
STAGE_TARGETS+= stage_includes
.if !empty(INCSLINKS)
STAGE_TARGETS+= stage_symlinks
STAGE_SYMLINKS.INCS= ${INCSLINKS}
.endif
.endif
.endif

.endif # ${MK_INCLUDES} != "no"
