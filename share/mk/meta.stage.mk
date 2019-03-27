# $FreeBSD$
# $Id: meta.stage.mk,v 1.55 2017/10/27 01:17:09 sjg Exp $
#
#	@(#) Copyright (c) 2011-2017, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

.ifndef NO_STAGING

.if !target(__${.PARSEFILE}__)
# the guard target is defined later

.if ${.MAKE.DEPENDFILE_PREFERENCE:U${.MAKE.DEPENDFILE}:M*.${MACHINE}} != ""
# this is generally safer anyway
_dirdep ?= ${RELDIR}.${MACHINE}
.else
_dirdep ?= ${RELDIR}
.endif

CLEANFILES+= .dirdep

# this allows us to trace dependencies back to their src dir
.dirdep:	.NOPATH
	@echo '${_dirdep}' > $@

.if defined(NO_POSIX_SHELL) || ${type printf:L:sh:Mbuiltin} == ""
_stage_file_basename = `basename $$f`
_stage_target_dirname = `dirname $$t`
.else
_stage_file_basename = $${f\#\#*/}
_stage_target_dirname = $${t%/*}
.endif

_OBJROOT ?= ${OBJROOT:U${OBJTOP:H}}
.if ${_OBJROOT:M*/} != ""
_objroot ?= ${_OBJROOT:tA}/
.else
_objroot ?= ${_OBJROOT:tA}
.endif

# make sure this is global
_STAGED_DIRS ?=
.export _STAGED_DIRS
# add each dir we stage to to _STAGED_DIRS
# and make sure we have absolute paths so that bmake
# will match against .MAKE.META.BAILIWICK
STAGE_DIR_FILTER = tA:@d@$${_STAGED_DIRS::+=$$d}$$d@
# convert _STAGED_DIRS into suitable filters
GENDIRDEPS_FILTER += Nnot-empty-is-important \
	${_STAGED_DIRS:O:u:M${OBJTOP}*:S,${OBJTOP}/,N,} \
	${_STAGED_DIRS:O:u:M${_objroot}*:N${OBJTOP}*:S,${_objroot},,:C,^([^/]+)/(.*),N\2.\1,:S,${HOST_TARGET},.host,}

LN_CP_SCRIPT = LnCp() { \
  rm -f $$2 2> /dev/null; \
  { [ -z "$$mode" ] && ${LN:Uln} $$1 $$2 2> /dev/null; } || \
  cp -p $$1 $$2; }

# a staging conflict should cause an error
# a warning is handy when bootstapping different options.
STAGE_CONFLICT?= ERROR
.if ${STAGE_CONFLICT:tl} == "error"
STAGE_CONFLICT_ACTION= exit 1;
.else
STAGE_CONFLICT_ACTION=
.endif

# it is an error for more than one src dir to try and stage
# the same file
STAGE_DIRDEP_SCRIPT = ${LN_CP_SCRIPT}; StageDirdep() { \
  t=$$1; \
  if [ -s $$t.dirdep ]; then \
	cmp -s .dirdep $$t.dirdep && return; \
	echo "${STAGE_CONFLICT}: $$t installed by `cat $$t.dirdep` not ${_dirdep}" >&2; \
	${STAGE_CONFLICT_ACTION} \
  fi; \
  LnCp .dirdep $$t.dirdep || exit 1; }

# common logic for staging files
# this all relies on RELDIR being set to a subdir of SRCTOP
# we use ln(1) if we can, else cp(1)
STAGE_FILE_SCRIPT = ${STAGE_DIRDEP_SCRIPT}; StageFiles() { \
  case "$$1" in "") return;; -m) mode=$$2; shift 2;; *) mode=;; esac; \
  dest=$$1; shift; \
  mkdir -p $$dest; \
  [ -s .dirdep ] || echo '${_dirdep}' > .dirdep; \
  for f in "$$@"; do \
	case "$$f" in */*) t=$$dest/${_stage_file_basename};; *) t=$$dest/$$f;; esac; \
	StageDirdep $$t; \
	LnCp $$f $$t || exit 1; \
	[ -z "$$mode" ] || chmod $$mode $$t; \
  done; :; }

STAGE_LINKS_SCRIPT = ${STAGE_DIRDEP_SCRIPT}; StageLinks() { \
  case "$$1" in "") return;; --) shift;; -*) ldest= lnf=$$1; shift;; /*) ldest=$$1/;; esac; \
  dest=$$1; shift; \
  mkdir -p $$dest; \
  [ -s .dirdep ] || echo '${_dirdep}' > .dirdep; \
  while test $$\# -ge 2; do \
	l=$$ldest$$1; shift; \
	t=$$dest/$$1; \
	case "$$1" in */*) mkdir -p ${_stage_target_dirname};; esac; \
	shift; \
	StageDirdep $$t; \
	rm -f $$t 2>/dev/null; \
	ln $$lnf $$l $$t || exit 1; \
  done; :; }

STAGE_AS_SCRIPT = ${STAGE_DIRDEP_SCRIPT}; StageAs() { \
  case "$$1" in "") return;; -m) mode=$$2; shift 2;; *) mode=;; esac; \
  dest=$$1; shift; \
  mkdir -p $$dest; \
  [ -s .dirdep ] || echo '${_dirdep}' > .dirdep; \
  while test $$\# -ge 2; do \
	s=$$1; shift; \
	t=$$dest/$$1; \
	case "$$1" in */*) mkdir -p ${_stage_target_dirname};; esac; \
	shift; \
	StageDirdep $$t; \
	LnCp $$s $$t || exit 1; \
	[ -z "$$mode" ] || chmod $$mode $$t; \
  done; :; }

# this is simple, a list of the "staged" files depends on this,
_STAGE_BASENAME_USE:	.USE .dirdep ${.TARGET:T}
	@${STAGE_FILE_SCRIPT}; StageFiles ${.TARGET:H:${STAGE_DIR_FILTER}} ${.TARGET:T}

_STAGE_AS_BASENAME_USE:        .USE .dirdep ${.TARGET:T}
	@${STAGE_AS_SCRIPT}; StageAs ${.TARGET:H:${STAGE_DIR_FILTER}} ${.TARGET:T} ${STAGE_AS_${.TARGET:T}:U${.TARGET:T}}


.endif				# first time


.if !empty(STAGE_INCSDIR)
.if !empty(STAGE_INCS)
stage_incs: ${STAGE_INCS}
.endif
.if target(stage_incs) || !empty(.ALLTARGETS:Mstage_includes)
STAGE_TARGETS += stage_incs
STAGE_INCS ?= ${.ALLSRC:N.dirdep:Nstage_*}
stage_includes: stage_incs
stage_incs:	.dirdep
	@${STAGE_FILE_SCRIPT}; StageFiles ${STAGE_INCSDIR:${STAGE_DIR_FILTER}} ${STAGE_INCS}
	@touch $@

.endif
.endif

.if !empty(STAGE_LIBDIR)
.if !empty(STAGE_LIBS)
stage_libs: ${STAGE_LIBS}
.endif
.if target(stage_libs)
STAGE_TARGETS += stage_libs
STAGE_LIBS ?= ${.ALLSRC:N.dirdep:Nstage_*}
stage_libs:	.dirdep
	@${STAGE_FILE_SCRIPT}; StageFiles ${STAGE_LIBDIR:${STAGE_DIR_FILTER}} ${STAGE_LIBS}
.if !defined(NO_SHLIB_LINKS)
.if !empty(SHLIB_LINKS)
	@${STAGE_LINKS_SCRIPT}; StageLinks -s ${STAGE_LIBDIR:${STAGE_DIR_FILTER}} \
	${SHLIB_LINKS:@t@${STAGE_LIBS:T:M$t.*} $t@}
.elif !empty(SHLIB_LINK) && !empty(SHLIB_NAME)
	@${STAGE_LINKS_SCRIPT}; StageLinks -s ${STAGE_LIBDIR:${STAGE_DIR_FILTER}} ${SHLIB_NAME} ${SHLIB_LINK}
.endif
.endif
	@touch $@
.endif
.endif

.if !empty(STAGE_DIR)
STAGE_SETS += _default
STAGE_DIR._default = ${STAGE_DIR}
STAGE_LINKS_DIR._default = ${STAGE_LINKS_DIR:U${STAGE_OBJTOP}}
STAGE_SYMLINKS_DIR._default = ${STAGE_SYMLINKS_DIR:U${STAGE_OBJTOP}}
STAGE_FILES._default = ${STAGE_FILES}
STAGE_LINKS._default = ${STAGE_LINKS}
STAGE_SYMLINKS._default = ${STAGE_SYMLINKS}
.endif

.if !empty(STAGE_SETS)
CLEANFILES += ${STAGE_SETS:@s@stage*$s@}

# some makefiles need to populate multiple directories
.for s in ${STAGE_SETS:O:u}
.if !empty(STAGE_FILES.$s)
stage_files.$s: ${STAGE_FILES.$s}
.endif
.if target(stage_files.$s) || target(stage_files${s:S,^,.,:N._default})
STAGE_TARGETS += stage_files
STAGE_FILES.$s ?= ${.ALLSRC:N.dirdep:Nstage_*}
.if !target(.stage_files.$s)
.stage_files.$s:
.if $s != "_default"
stage_files:	stage_files.$s
stage_files.$s:	.dirdep
.else
STAGE_FILES ?= ${.ALLSRC:N.dirdep:Nstage_*}
stage_files:	.dirdep
.endif
	@${STAGE_FILE_SCRIPT}; StageFiles ${FLAGS.$@} ${STAGE_FILES_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_FILES.$s}
	@touch $@
.endif
.endif

.if !empty(STAGE_LINKS.$s)
stage_links.$s:
.endif
.if target(stage_links.$s) || target(stage_links${s:S,^,.,:N._default})
STAGE_LINKS_DIR.$s ?= ${STAGE_OBJTOP}
STAGE_TARGETS += stage_links
.if !target(.stage_links.$s)
.stage_links.$s:
.if $s != "_default"
stage_links:	stage_links.$s
stage_links.$s:	.dirdep
.else
stage_links:	.dirdep
.endif
	@${STAGE_LINKS_SCRIPT}; StageLinks ${STAGE_LINKS_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_LINKS.$s}
	@touch $@
.endif
.endif

.if !empty(STAGE_SYMLINKS.$s)
stage_symlinks.$s:
.endif
.if target(stage_symlinks.$s) || target(stage_symlinks${s:S,^,.,:N._default})
STAGE_SYMLINKS_DIR.$s ?= ${STAGE_OBJTOP}
STAGE_TARGETS += stage_symlinks
.if !target(.stage_symlinks.$s)
.stage_symlinks.$s:
.if $s != "_default"
stage_symlinks:	stage_symlinks.$s
stage_symlinks.$s:	.dirdep
.else
stage_symlinks:	.dirdep
.endif
	@${STAGE_LINKS_SCRIPT}; StageLinks -s ${STAGE_SYMLINKS_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_SYMLINKS.$s}
	@touch $@
.endif
.endif

.endfor
.endif

.if !empty(STAGE_AS_SETS)
CLEANFILES += ${STAGE_AS_SETS:@s@stage*$s@}

# sometimes things need to be renamed as they are staged
# each ${file} will be staged as ${STAGE_AS_${file:T}}
# one could achieve the same with SYMLINKS
# stage_as_and_symlink makes the original name a symlink to the new name
# it is the same as using stage_as and stage_symlinks but ensures
# both operations happen together
.for s in ${STAGE_AS_SETS:O:u}
.if !empty(STAGE_AS.$s)
stage_as.$s: ${STAGE_AS.$s}
.endif
.if target(stage_as.$s)
STAGE_TARGETS += stage_as
STAGE_AS.$s ?= ${.ALLSRC:N.dirdep:Nstage_*}
.if !target(.stage_as.$s)
.stage_as.$s:
stage_as:	stage_as.$s
stage_as.$s:	.dirdep
	@${STAGE_AS_SCRIPT}; StageAs ${FLAGS.$@} ${STAGE_FILES_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_AS.$s:@f@$f ${STAGE_AS_${f:tA}:U${STAGE_AS_${f:T}:U${f:T}}}@}
	@touch $@
.endif
.endif

.if !empty(STAGE_AS_AND_SYMLINK.$s)
stage_as_and_symlink.$s: ${STAGE_AS_AND_SYMLINK.$s}
.endif
.if target(stage_as_and_symlink.$s)
STAGE_TARGETS += stage_as_and_symlink
STAGE_AS_AND_SYMLINK.$s ?= ${.ALLSRC:N.dirdep:Nstage_*}
.if !target(.stage_as_and_symlink.$s)
.stage_as_and_symlink.$s:
stage_as_and_symlink:	stage_as_and_symlink.$s
stage_as_and_symlink.$s:	.dirdep
	@${STAGE_AS_SCRIPT}; StageAs ${FLAGS.$@} ${STAGE_FILES_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_AS_AND_SYMLINK.$s:@f@$f ${STAGE_AS_${f:tA}:U${STAGE_AS_${f:T}:U${f:T}}}@}
	@${STAGE_LINKS_SCRIPT}; StageLinks -s ${STAGE_FILES_DIR.$s:U${STAGE_DIR.$s}:${STAGE_DIR_FILTER}} ${STAGE_AS_AND_SYMLINK.$s:@f@${STAGE_AS_${f:tA}:U${STAGE_AS_${f:T}:U${f:T}}} $f@}
	@touch $@
.endif
.endif

.endfor
.endif

CLEANFILES += ${STAGE_TARGETS} stage_incs stage_includes

# this lot also only makes sense the first time...
.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

# stage_*links usually needs to follow any others.
# for non-jobs mode the order here matters
staging: ${STAGE_TARGETS:N*_links} ${STAGE_TARGETS:M*_links}

.if ${.MAKE.JOBS:U0} > 0 && ${STAGE_TARGETS:U:M*_links} != ""
# the above isn't sufficient
.for t in ${STAGE_TARGETS:N*links:O:u}
.ORDER: $t stage_links
.endfor
.endif

# generally we want staging to wait until everything else is done
STAGING_WAIT ?= .WAIT

.if ${.MAKE.LEVEL} > 0
all: ${STAGING_WAIT} staging
.endif

.if exists(${.PARSEDIR}/stage-install.sh) && !defined(STAGE_INSTALL)
# this will run install(1) and then followup with .dirdep files.
STAGE_INSTALL := sh ${.PARSEDIR:tA}/stage-install.sh INSTALL="${INSTALL}" OBJDIR=${.OBJDIR:tA}
.endif

# if ${INSTALL} gets run during 'all' assume it is for staging?
.if ${.TARGETS:Nall} == "" && defined(STAGE_INSTALL)
INSTALL := ${STAGE_INSTALL}
.if target(beforeinstall)
beforeinstall: .dirdep
.endif
.endif
.NOPATH: ${STAGE_FILES}

.if !empty(STAGE_TARGETS)
# for backwards compat make sure they exist
${STAGE_TARGETS}:

.NOPATH: ${CLEANFILES}

MK_STALE_STAGED?= no
.if ${MK_STALE_STAGED} == "yes"
all: stale_staged
# get a list of paths that we have just staged
# get a list of paths that we have previously staged to those same dirs
# anything in the 2nd list but not the first is stale - remove it.
stale_staged: staging .NOMETA
	@egrep '^[WL] .*${STAGE_OBJTOP}' /dev/null ${.MAKE.META.FILES:M*stage_*} | \
	sed "/\.dirdep/d;s,.* '*\(${STAGE_OBJTOP}/[^ '][^ ']*\).*,\1," | \
	sort > ${.TARGET}.staged1
	@grep -l '${_dirdep}' /dev/null ${_STAGED_DIRS:M${STAGE_OBJTOP}*:O:u:@d@$d/*.dirdep@} | \
	sed 's,\.dirdep,,' | sort > ${.TARGET}.staged2
	@comm -13 ${.TARGET}.staged1 ${.TARGET}.staged2 > ${.TARGET}.stale
	@test ! -s ${.TARGET}.stale || { \
		echo "Removing stale staged files..."; \
		sed 's,.*,& &.dirdep,' ${.TARGET}.stale | xargs rm -f; }

.endif
.endif
.endif
.endif
