# $FreeBSD$

.if !target(__<bsd.init.mk>__)
.error bsd.files.mk cannot be included directly.
.endif

.if !target(__<bsd.files.mk>__)
.if target(__<bsd.dirs.mk>__)
.error bsd.dirs.mk must be included after bsd.files.mk.
.endif

__<bsd.files.mk>__:

FILESGROUPS?=	FILES

.for group in ${FILESGROUPS}
# Add in foo.yes and remove duplicates from all the groups
${${group}}:= ${${group}} ${${group}.yes}
${${group}}:= ${${group}:O:u}
buildfiles: ${${group}}
.endfor

.if !defined(_SKIP_BUILD)
all: buildfiles
.endif

.for group in ${FILESGROUPS}
.if defined(${group}) && !empty(${group})
installfiles: installfiles-${group}

${group}OWN?=	${SHAREOWN}
${group}GRP?=	${SHAREGRP}
.if ${MK_INSTALL_AS_USER} == "yes"
${group}OWN=	${SHAREOWN}
${group}GRP=	${SHAREGRP}
.endif
${group}MODE?=	${SHAREMODE}
${group}DIR?=	BINDIR
STAGE_SETS+=	${group:C,[/*],_,g}

.if defined(NO_ROOT)
.if !defined(${group}TAGS) || ! ${${group}TAGS:Mpackage=*}
${group}TAGS+=		package=${${group}PACKAGE:Uruntime}
.endif
${group}TAG_ARGS=	-T ${${group}TAGS:[*]:S/ /,/g}
.endif


.if ${${group}DIR:S/^\///} == ${${group}DIR}
# ${group}DIR specifies a variable that specifies a path
DIRS+=	${${group}DIR}
_${group}DIR=	${${group}DIR}
.else
# ${group}DIR specifies a path
DIRS+=	${group}DIR
_${group}DIR=	${group}DIR
.endif

STAGE_DIR.${group:C,[/*],_,g}= ${STAGE_OBJTOP}${${_${group}DIR}}

.for file in ${${group}}
${group}OWN_${file}?=	${${group}OWN}
${group}GRP_${file}?=	${${group}GRP}
.if ${MK_INSTALL_AS_USER} == "yes"
${group}OWN_${file}=	${SHAREOWN}
${group}GRP_${file}=	${SHAREGRP}
.endif # ${MK_INSTALL_AS_USER} == "yes"
${group}MODE_${file}?=	${${group}MODE}

# Determine the directory for the current file.  Default to the parent group
# DIR, then check to see how to pass that variable on below.
${group}DIR_${file}?=	${${group}DIR}
.if ${${group}DIR_${file}:S/^\///} == ${${group}DIR_${file}}
# DIR specifies a variable that specifies a path
_${group}DIR_${file}=	${${group}DIR_${file}}
.else
# DIR directly specifies a path
_${group}DIR_${file}=	${group}DIR_${file}
.endif
${group}PREFIX_${file}=	${DESTDIR}${${_${group}DIR_${file}}}

# Append DIR to DIRS if not already in place -- DIRS is already filtered, so
# this is primarily to ease inspection.
.for d in ${DIRS}
_DIRS+=	${${d}}
.endfor
.if ${DIRS:M${_${group}DIR_${file}}} == ""
.if ${_DIRS:M${${_${group}DIR_${file}}}} == ""
DIRS+=	${_${group}DIR_${file}}
.else
_${group}DIR_${file}=	${group}DIR
.endif
.endif

.if defined(${group}NAME)
${group}NAME_${file}?=	${${group}NAME}
.else
${group}NAME_${file}?=	${file:T}
.endif # defined(${group}NAME)
STAGE_AS_SETS+=	${file}
STAGE_AS_${file}= ${${group}NAME_${file}}
# XXX {group}OWN,GRP,MODE
STAGE_DIR.${file}= ${STAGE_OBJTOP}${${_${group}DIR_${file}}}
stage_as.${file}: ${file}

installfiles-${group}: _${group}INS1_${file}
_${group}INS1_${file}: installdirs-${_${group}DIR_${file}} _${group}INS_${file}
_${group}INS_${file}: ${file}
	${INSTALL} ${${group}TAG_ARGS} -o ${${group}OWN_${file}} \
	    -g ${${group}GRP_${file}} -m ${${group}MODE_${file}} \
	    ${.ALLSRC} ${${group}PREFIX_${file}}/${${group}NAME_${file}}
.endfor # file in ${${group}}

.endif # defined(${group}) && !empty(${group})
.endfor # .for group in ${FILESGROUPS}

realinstall: installfiles
.ORDER: beforeinstall installfiles

.if ${MK_STAGING} != "no"
.if !empty(STAGE_SETS)
buildfiles: stage_files
STAGE_TARGETS+= stage_files
.if !empty(STAGE_AS_SETS)
buildfiles: stage_as
STAGE_TARGETS+= stage_as
.endif
.endif
.endif

.include <bsd.dirs.mk>

.endif # !target(__<bsd.files.mk>__)
