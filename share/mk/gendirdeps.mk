# $FreeBSD$
# $Id: gendirdeps.mk,v 1.39 2018/06/08 01:25:31 sjg Exp $

# Copyright (c) 2010-2013, Juniper Networks, Inc.
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions 
# are met: 
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer. 
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.  
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

#
# This makefile [re]generates ${.MAKE.DEPENDFILE}
#

.include <install-new.mk>

# Assumptions:
#	RELDIR is the relative path from ${SRCTOP} to ${_CURDIR}
#		(SRCTOP is ${SB}/src)
#	_CURDIR is the absolute version of ${.CURDIR}
#	_OBJDIR is the absolute version of ${.OBJDIR}
#	_objroot is realpath of ${_OBJTOP} without ${MACHINE}
#		this may be different from _OBJROOT if $SB/obj is a
#		symlink to another filesystem.
#		_objroot must be a prefix match for _objtop

.MAIN: all

# keep this simple
.MAKE.MODE = compat

all:

_CURDIR ?= ${.CURDIR}
_OBJDIR ?= ${.OBJDIR}
_OBJTOP ?= ${OBJTOP}
_OBJROOT ?= ${OBJROOT:U${_OBJTOP}}
.if ${_OBJROOT:M*/}
_slash=/
.else
_slash=
.endif
_objroot ?= ${_OBJROOT:tA}${_slash}

_this = ${.PARSEDIR}/${.PARSEFILE}

# remember what to make
_DEPENDFILE := ${_CURDIR}/${.MAKE.DEPENDFILE:T}

# We do _not_ want to read our own output!
.MAKE.DEPENDFILE = /dev/null

# caller should have set this
META_FILES ?= ${.MAKE.META.FILES}

.if !empty(META_FILES)

.if ${.MAKE.LEVEL} > 0 && !empty(GENDIRDEPS_FILTER)
# so we can compare below
.-include <${_DEPENDFILE}>
# yes, I mean :U with no value
_DIRDEPS := ${DIRDEPS:U:O:u}
.endif

META_FILES := ${META_FILES:T:O:u}
.export META_FILES

# pickup customizations
.-include <local.gendirdeps.mk>

# these are actually prefixes that we'll skip
# they should all be absolute paths
SKIP_GENDIRDEPS ?=
.if !empty(SKIP_GENDIRDEPS)
_skip_gendirdeps = egrep -v '^(${SKIP_GENDIRDEPS:O:u:ts|})' |
.else
_skip_gendirdeps =
.endif

# Below we will turn _{VAR} into ${VAR} which keeps this simple
# GENDIRDEPS_FILTER_DIR_VARS is a list of dirs to be substiuted for.
# GENDIRDEPS_FILTER_VARS is more general.
# In each case order matters.
.if !empty(GENDIRDEPS_FILTER_DIR_VARS)
GENDIRDEPS_FILTER += ${GENDIRDEPS_FILTER_DIR_VARS:@v@S,${$v},_{${v}},@}
.endif
.if !empty(GENDIRDEPS_FILTER_VARS)
GENDIRDEPS_FILTER += ${GENDIRDEPS_FILTER_VARS:@v@S,/${$v}/,/_{${v}}/,@:NS,//,*:u}
.endif

# this (*should* be set in meta.sys.mk) 
# is the script that extracts what we want.
META2DEPS ?= ${.PARSEDIR}/meta2deps.sh
META2DEPS := ${META2DEPS}

.if ${DEBUG_GENDIRDEPS:Uno:@x@${RELDIR:M$x}@} != "" && ${DEBUG_GENDIRDEPS:Uno:Mmeta2d*} != ""
_time = time
_sh_x = sh -x
_py_d = -ddd
.else
_time =
_sh_x =
_py_d =
.endif

.if ${META2DEPS:E} == "py"
# we can afford to do this all the time.
DPDEPS ?= no
META2DEPS_CMD = ${_time} ${PYTHON} ${META2DEPS} ${_py_d} 
.if ${DPDEPS:tl} != "no"
META2DEPS_CMD += -D ${DPDEPS}
.endif
META2DEPS_FILTER = sed 's,^src:,${SRCTOP}/,;s,^\([^/]\),${OBJTOP}/\1,' |
.elif ${META2DEPS:E} == "sh"
META2DEPS_CMD = ${_time} ${_sh_x} ${META2DEPS} OBJTOP=${_OBJTOP}
.else
META2DEPS_CMD ?= ${META2DEPS}
.endif

.if ${TARGET_OBJ_SPEC:U${MACHINE}} != ${MACHINE}
META2DEPS_CMD += -T ${TARGET_OBJ_SPEC}
.endif
META2DEPS_CMD += \
	-R ${RELDIR} -H ${HOST_TARGET} \
	${M2D_OBJROOTS:O:u:@o@-O $o@} \
	${M2D_EXCLUDES:O:u:@o@-X $o@} \


M2D_OBJROOTS += ${OBJTOP} ${_OBJROOT} ${_objroot}
.if defined(SB_OBJROOT)
M2D_OBJROOTS += ${SB_OBJROOT}
.endif
.if defined(STAGE_ROOT)
M2D_OBJROOTS += ${STAGE_ROOT}
.endif
.if ${.MAKE.DEPENDFILE_PREFERENCE:U${.MAKE.DEPENDFILE}:M*.${MACHINE}} == ""
# meta2deps.py only groks objroot
# so we need to give it what it expects
# and tell it not to add machine qualifiers
META2DEPS_ARGS += MACHINE=none
.endif
.if defined(SB_BACKING_SB) 
META2DEPS_CMD += -S ${SB_BACKING_SB}/src 
M2D_OBJROOTS += ${SB_BACKING_SB}/${SB_OBJPREFIX}
.endif

GENDIRDEPS_SEDCMDS += \
	-e 's,//*$$,,;s,\.${HOST_TARGET:Uhost}$$,.host,' \
	-e 's,\.${HOST_TARGET32:Uhost32}$$,.host32,' \
	-e 's,\.${MACHINE}$$,,' \
	-e 's:\.${TARGET_SPEC:U${MACHINE}}$$::'

# we are only interested in the dirs
# specifically those we read something from.
# we canonicalize them to keep things simple
# if we are using a split-fs sandbox, it gets a little messier.
_objtop := ${_OBJTOP:tA}

# some people put *.meta in META_XTRAS to make sure we get here
_meta_files := ${META_FILES:N\*.meta:O:u}
# assume a big list
_meta_files_arg= @meta.list
.if empty(_meta_files) && ${META_FILES:M\*.meta} != ""
# XXX this should be considered a bad idea, 
# since we cannot ignore stale .meta
x != cd ${_OBJDIR} && find . -name '*.meta' -print -o \( -type d ! -name . -prune \) | sed 's,^./,,' > meta.list; echo
.elif ${_meta_files:[#]} > 500
.export _meta_files
x != echo; for m in $$_meta_files; do echo $$m; done > meta.list
.else
_meta_files_arg:= ${_meta_files}
.endif

dir_list != cd ${_OBJDIR} && \
	${META2DEPS_CMD} MACHINE=${MACHINE} \
	SRCTOP=${SRCTOP} RELDIR=${RELDIR} CURDIR=${_CURDIR} \
	${META2DEPS_ARGS} \
	${_meta_files_arg} | ${META2DEPS_FILTER} ${_skip_gendirdeps} \
	sed ${GENDIRDEPS_SEDCMDS}

.if ${dir_list:M*ERROR\:*} != ""
.warning ${dir_list:tW:C,.*(ERROR),\1,}
.warning Skipping ${_DEPENDFILE:S,${SRCTOP}/,,}
# we are not going to update anything
.else
dpadd_dir_list=
.if !empty(DPADD)
_nonlibs := ${DPADD:T:Nlib*:N*include}
.if !empty(_nonlibs)
ddep_list =
.for f in ${_nonlibs:@x@${DPADD:M*/$x}@}
.if exists($f.dirdep)
ddep_list += $f.dirdep
.elif exists(${f:H}.dirdep)
ddep_list += ${f:H}.dirdep
.else
dir_list += ${f:H:tA}
dpadd_dir_list += ${f:H:tA}
.endif
.endfor
.if !empty(ddep_list)
ddeps != cat ${ddep_list:O:u} | ${META2DEPS_FILTER} ${_skip_gendirdeps} \
	sed ${GENDIRDEPS_SEDCMDS}

.if ${DEBUG_GENDIRDEPS:Uno:@x@${RELDIR:M$x}@} != ""
.info ${RELDIR}: raw_dir_list='${dir_list}'
.info ${RELDIR}: ddeps='${ddeps}'
.endif
dir_list += ${ddeps}
.endif
.endif
.endif

# DIRDEPS represent things that had to have been built first
# so they should all be undir OBJTOP.
# Note that ${_OBJTOP}/bsd/include/machine will get reported 
# to us as $SRCTOP/bsd/sys/$MACHINE_ARCH/include meaning we
# will want to visit bsd/include
# so we add 
# ${"${dir_list:M*bsd/sys/${MACHINE_ARCH}/include}":?bsd/include:}
# to GENDIRDEPS_DIR_LIST_XTRAS
_objtops = ${OBJTOP} ${_OBJTOP} ${_objtop}
_objtops := ${_objtops:O:u}
dirdep_list = \
	${_objtops:@o@${dir_list:M$o*/*:C,$o[^/]*/,,}@} \
	${GENDIRDEPS_DIR_LIST_XTRAS}

# sort longest first
M2D_OBJROOTS := ${M2D_OBJROOTS:O:u:[-1..1]}

# anything we use from an object dir other than ours
# needs to be qualified with its .<machine> suffix
# (we used the pseudo machine "host" for the HOST_TARGET).
skip_ql= ${SRCTOP}* ${_objtops:@o@$o*@}
.for o in ${M2D_OBJROOTS:${skip_ql:${M_ListToSkip}}}
# we need := so only skip_ql to this point applies
ql.$o := ${dir_list:${skip_ql:${M_ListToSkip}}:M$o*/*/*:C,$o([^/]+)/(.*),\2.\1,:S,.${HOST_TARGET},.host,}
qualdir_list += ${ql.$o}
.if ${DEBUG_GENDIRDEPS:Uno:@x@${RELDIR:M$x}@} != ""
.info ${RELDIR}: o=$o ${ql.$o qualdir_list:L:@v@$v=${$v}@}
.endif
skip_ql+= $o*
.endfor

dirdep_list := ${dirdep_list:O:u}
qualdir_list := ${qualdir_list:N*.${MACHINE}:O:u}

DIRDEPS = \
	${dirdep_list:N${RELDIR}:N${RELDIR}/*} \
	${qualdir_list:N${RELDIR}.*:N${RELDIR}/*}

# We only consider things below $RELDIR/ if they have a makefile.
# This is the same test that _DIRDEP_USE applies.
# We have do a double test with dirdep_list as it _may_ contain 
# qualified dirs - if we got anything from a stage dir.
# qualdir_list we know are all qualified.
# It would be nice do peform this check for all of DIRDEPS,
# but we cannot assume that all of the tree is present, 
# in fact we can only assume that RELDIR is.
DIRDEPS += \
	${dirdep_list:M${RELDIR}/*:@d@${.MAKE.MAKEFILE_PREFERENCE:@m@${exists(${SRCTOP}/$d/$m):?$d:${exists(${SRCTOP}/${d:R}/$m):?$d:}}@}@} \
	${qualdir_list:M${RELDIR}/*:@d@${.MAKE.MAKEFILE_PREFERENCE:@m@${exists(${SRCTOP}/${d:R}/$m):?$d:}@}@}

# what modifiers do we allow in GENDIRDEPS_FILTER
GENDIRDEPS_FILTER_MASK += @CMNS
DIRDEPS := ${DIRDEPS:${GENDIRDEPS_FILTER:UNno:M[${GENDIRDEPS_FILTER_MASK:O:u:ts}]*:ts:}:C,//+,/,g:O:u}

.if ${DEBUG_GENDIRDEPS:Uno:@x@${RELDIR:M$x}@} != ""
.info ${RELDIR}: M2D_OBJROOTS=${M2D_OBJROOTS}
.info ${RELDIR}: M2D_EXCLUDES=${M2D_EXCLUDES}
.info ${RELDIR}: dir_list='${dir_list}'
.info ${RELDIR}: dpadd_dir_list='${dpadd_dir_list}'
.info ${RELDIR}: dirdep_list='${dirdep_list}'
.info ${RELDIR}: qualdir_list='${qualdir_list}'
.info ${RELDIR}: SKIP_GENDIRDEPS='${SKIP_GENDIRDEPS}'
.info ${RELDIR}: GENDIRDEPS_FILTER='${GENDIRDEPS_FILTER}'
.info ${RELDIR}: FORCE_DPADD='${DPADD}'
.info ${RELDIR}: DIRDEPS='${DIRDEPS}'
.endif

# SRC_DIRDEPS is for checkout logic
src_dirdep_list = \
	${dir_list:M${SRCTOP}/*:S,${SRCTOP}/,,}

SRC_DIRDEPS = \
	${src_dirdep_list:N${RELDIR}:N${RELDIR}/*:C,(/h)/.*,,}

SRC_DIRDEPS := ${SRC_DIRDEPS:${GENDIRDEPS_SRC_FILTER:UN/*:ts:}:C,//+,/,g:O:u}

# if you want to capture SRC_DIRDEPS in .MAKE.DEPENDFILE put
# SRC_DIRDEPS_FILE = ${_DEPENDFILE} 
# in local.gendirdeps.mk
.if ${SRC_DIRDEPS_FILE:Uno:tl} != "no"
ECHO_SRC_DIRDEPS = echo 'SRC_DIRDEPS = \'; echo '${SRC_DIRDEPS:@d@	$d \\${.newline}@}'; echo;

.if ${SRC_DIRDEPS_FILE:T} == ${_DEPENDFILE:T}
_include_src_dirdeps = ${ECHO_SRC_DIRDEPS}
.else
all: ${SRC_DIRDEPS_FILE}
.if !target(${SRC_DIRDEPS_FILE})
${SRC_DIRDEPS_FILE}: ${META_FILES} ${_this} ${META2DEPS}
	@(${ECHO_SRC_DIRDEPS}) > $@
.endif
.endif
.endif
_include_src_dirdeps ?= 

all:	${_DEPENDFILE}

# if this is going to exist it would be there by now
.if !exists(.depend)
CAT_DEPEND = /dev/null
.endif
CAT_DEPEND ?= .depend

.if !empty(_DIRDEPS) && ${DIRDEPS} != ${_DIRDEPS}
# we may have changed a filter
.PHONY: ${_DEPENDFILE}
.endif

# 'cat .depend' should suffice, but if we are mixing build modes
# .depend may contain things we don't want.
# The sed command at the end of the stream, allows for the filters
# to output _{VAR} tokens which we will turn into proper ${VAR} references.
${_DEPENDFILE}: .NOMETA ${CAT_DEPEND:M.depend} ${META_FILES:O:u:@m@${exists($m):?$m:}@} ${_this} ${META2DEPS}
	@(${GENDIRDEPS_HEADER} echo '# Autogenerated - do NOT edit!'; echo; \
	echo 'DIRDEPS = \'; \
	echo '${DIRDEPS:@d@	$d \\${.newline}@}'; echo; \
	${_include_src_dirdeps} \
	echo '.include <dirdeps.mk>'; \
	echo; \
	echo '.if $${DEP_RELDIR} == $${_DEP_RELDIR}'; \
	echo '# local dependencies - needed for -jN in clean tree'; \
	[ -s ${CAT_DEPEND} ] && { grep : ${CAT_DEPEND} | grep -v '[/\\]'; }; \
	echo '.endif' ) | sed 's,_\([{(]\),$$\1,g' > $@.new${.MAKE.PID}
	@${InstallNew}; InstallNew -s $@.new${.MAKE.PID}

.endif				# meta2deps failed
.elif !empty(SUBDIR)

DIRDEPS := ${SUBDIR:S,^,${RELDIR}/,:O:u}

all:	${_DEPENDFILE}

${_DEPENDFILE}: .NOMETA ${MAKEFILE} ${_this}
	@(${GENDIRDEPS_HEADER} echo '# Autogenerated - do NOT edit!'; echo; \
	echo 'DIRDEPS = \'; \
	echo '${DIRDEPS:@d@	$d \\${.newline}@}'; echo; \
	echo '.include <dirdeps.mk>'; \
	echo ) | sed 's,_\([{(]\),$$\1,g' > $@.new
	@${InstallNew}; InstallNew $@.new

.else

# nothing to do
all ${_DEPENDFILE}:

.endif
${_DEPENDFILE}: .PRECIOUS
