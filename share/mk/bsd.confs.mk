# $FreeBSD$

.if !target(__<bsd.init.mk>__)
.  error bsd.conf.mk cannot be included directly.
.endif

.if !target(__<bsd.confs.mk>__)
.  if target(__<bsd.dirs.mk>__)
.    error bsd.dirs.mk must be included after bsd.confs.mk.
.  endif

__<bsd.confs.mk>__:

CONFGROUPS?=	CONFS

.  if !target(buildconfig)
.    for group in ${CONFGROUPS}
buildconfig: ${${group}}
.    endfor
.  endif

.  if !defined(_SKIP_BUILD)
all: buildconfig
.  endif

.  for group in ${CONFGROUPS}
.    if defined(${group}) && !empty(${group})

.      if !target(afterinstallconfig)
afterinstallconfig:
.      endif
.      if !target(beforeinstallconfig)
beforeinstallconfig:
.      endif
installconfig:	beforeinstallconfig realinstallconfig afterinstallconfig
.ORDER:		beforeinstallconfig realinstallconfig afterinstallconfig

${group}OWN?=	${SHAREOWN}
${group}GRP?=	${SHAREGRP}
${group}MODE?=	${CONFMODE}
${group}DIR?=	${CONFDIR}
STAGE_SETS+=	${group:C,[/*],_,g}

.      if defined(NO_ROOT)
.        if !defined(${group}TAGS) || ! ${${group}TAGS:Mpackage=*}
.          if defined(${${group}PACKAGE})
${group}TAGS+=		package=${${group}PACKAGE:Uruntime}
.          else
${group}TAGS+=		package=${PACKAGE:Uruntime}
.          endif
.        endif
${group}TAGS+=		config
${group}TAG_ARGS=	-T ${${group}TAGS:[*]:S/ /,/g}
.      endif


.      if ${${group}DIR:S/^\///} == ${${group}DIR}
# ${group}DIR specifies a variable that specifies a path
DIRS+=	${${group}DIR}
_${group}DIR=	${${group}DIR}
.      else
# ${group}DIR specifies a path
DIRS+=	${group}DIR
_${group}DIR=	${group}DIR
.      endif

STAGE_DIR.${group:C,[/*],_,g}= ${STAGE_OBJTOP}${${_${group}DIR}}

.      for cnf in ${${group}}
${group}OWN_${cnf}?=	${${group}OWN}
${group}GRP_${cnf}?=	${${group}GRP}
${group}MODE_${cnf}?=	${${group}MODE}
${group}DIR_${cnf}?=	${${group}DIR}
.        if defined(${group}NAME)
${group}NAME_${cnf}?=	${${group}NAME}
.        else
${group}NAME_${cnf}?=	${cnf:T}
.        endif


# Determine the directory for the current file.  Default to the parent group
# DIR, then check to see how to pass that variable on below.
${group}DIR_${cnf}?=	${${group}DIR}
.        if ${${group}DIR_${cnf}:S/^\///} == ${${group}DIR_${cnf}}
# DIR specifies a variable that specifies a path
_${group}DIR_${cnf}=	${${group}DIR_${cnf}}
.        else
# DIR directly specifies a path
_${group}DIR_${cnf}=	${group}DIR_${cnf}
.        endif
${group}PREFIX_${cnf}=	${DESTDIR}${${_${group}DIR_${cnf}}}

# Append DIR to DIRS if not already in place -- DIRS is already filtered, so
# this is primarily to ease inspection.
.        for d in ${DIRS}
_DIRS+=	${${d}}
.        endfor
.        if ${DIRS:M${_${group}DIR_${cnf}}} == ""
.          if ${_DIRS:M${${_${group}DIR_${cnf}}}} == ""
DIRS+=	${_${group}DIR_${cnf}}
.          else
_${group}DIR_${cnf}=	${group}DIR
.          endif
.        endif

.        if defined(${group}NAME)
${group}NAME_${cnf}?=	${${group}NAME}
.        else
${group}NAME_${cnf}?=	${cnf:T}
.        endif # defined(${group}NAME)

# Work around a bug with install(1) -C and /dev/null
.        if ${cnf} == "/dev/null"
INSTALL_COPY=
.        else
INSTALL_COPY=  -C
.        endif

STAGE_AS_SETS+= ${cnf:T}
STAGE_AS_${cnf:T}= ${${group}NAME_${cnf:T}}
# XXX {group}OWN,GRP,MODE
STAGE_DIR.${cnf:T}= ${STAGE_OBJTOP}${${_${group}DIR_${cnf}}}
stage_as.${cnf:T}: ${cnf}

realinstallconfig: installdirs-${_${group}DIR_${cnf}} _${group}INS_${cnf:T}
_${group}INS_${cnf:T}: ${cnf}
	${INSTALL} ${${group}TAG_ARGS} ${INSTALL_COPY} -o ${${group}OWN_${cnf}} \
	    -g ${${group}GRP_${cnf}} -m ${${group}MODE_${cnf}} \
	    ${.ALLSRC} ${${group}PREFIX_${cnf}}/${${group}NAME_${cnf}}
.      endfor # for cnf in ${${group}}

.    endif # defined(${group}) && !empty(${group})
.  endfor

.if ${MK_STAGING} != "no"
.  if !empty(STAGE_SETS)
buildconfig: stage_files
.    if !empty(STAGE_AS_SETS)
buildconfig: stage_as
.    endif
.  endif
.endif

.endif # !target(__<bsd.confs.mk>__)
