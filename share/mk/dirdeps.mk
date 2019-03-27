# $FreeBSD$
# $Id: dirdeps.mk,v 1.96 2018/06/20 22:26:39 sjg Exp $

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

# Much of the complexity here is for supporting cross-building.
# If a tree does not support that, simply using plain Makefile.depend
# should provide sufficient clue.
# Otherwise the recommendation is to use Makefile.depend.${MACHINE}
# as expected below.

# Note: this file gets multiply included.
# This is what we do with DIRDEPS

# DIRDEPS:
#	This is a list of directories - relative to SRCTOP, it is
#	normally only of interest to .MAKE.LEVEL 0.
#	In some cases the entry may be qualified with a .<machine>
#	or .<target_spec> suffix (see TARGET_SPEC_VARS below),
#	for example to force building something for the pseudo
#	machines "host" or "common" regardless of current ${MACHINE}.
#	
#	All unqualified entries end up being qualified with .${TARGET_SPEC}
#	and partially qualified (if TARGET_SPEC_VARS has multiple
#	entries) are also expanded to a full .<target_spec>.
#	The  _DIRDEP_USE target uses the suffix to set TARGET_SPEC
#	correctly when visiting each entry.
#
#	The fully qualified directory entries are used to construct a
#	dependency graph that will drive the build later.
#	
#	Also, for each fully qualified directory target, we will search
#	using ${.MAKE.DEPENDFILE_PREFERENCE} to find additional
#	dependencies.  We use Makefile.depend (default value for
#	.MAKE.DEPENDFILE_PREFIX) to refer to these makefiles to
#	distinguish them from others.
#	
#	Before each Makefile.depend file is read, we set
#	DEP_RELDIR to be the RELDIR (path relative to SRCTOP) for
#	its directory, and DEP_MACHINE etc according to the .<target_spec>
#	represented by the suffix of the corresponding target.
#	
#	Since each Makefile.depend file includes dirdeps.mk, this
#	processing is recursive and results in .MAKE.LEVEL 0 learning the
#	dependencies of the tree wrt the initial directory (_DEP_RELDIR).
#
# BUILD_AT_LEVEL0
#	Indicates whether .MAKE.LEVEL 0 builds anything:
#	if "no" sub-makes are used to build everything,
#	if "yes" sub-makes are only used to build for other machines.
#	It is best to use "no", but this can require fixing some
#	makefiles to not do anything at .MAKE.LEVEL 0.
#
# TARGET_SPEC_VARS
#	The default value is just MACHINE, and for most environments
#	this is sufficient.  The _DIRDEP_USE target actually sets
#	both MACHINE and TARGET_SPEC to the suffix of the current
#	target so that in the general case TARGET_SPEC can be ignored.
#
#	If more than MACHINE is needed then sys.mk needs to decompose
#	TARGET_SPEC and set the relevant variables accordingly.
#	It is important that MACHINE be included in and actually be
#	the first member of TARGET_SPEC_VARS.  This allows other
#	variables to be considered optional, and some of the treatment
#	below relies on MACHINE being the first entry.
#	Note: TARGET_SPEC cannot contain any '.'s so the target
#	triple used by compiler folk won't work (directly anyway).
#
#	For example:
#
#		# Always list MACHINE first,
#		# other variables might be optional.
#		TARGET_SPEC_VARS = MACHINE TARGET_OS
#		.if ${TARGET_SPEC:Uno:M*,*} != ""
#		_tspec := ${TARGET_SPEC:S/,/ /g}
#		MACHINE := ${_tspec:[1]}
#		TARGET_OS := ${_tspec:[2]}
#		# etc.
#		# We need to stop that TARGET_SPEC affecting any submakes
#		# and deal with MACHINE=${TARGET_SPEC} in the environment.
#		TARGET_SPEC =
#		# export but do not track
#		.export-env TARGET_SPEC
#		.export ${TARGET_SPEC_VARS}
#		.for v in ${TARGET_SPEC_VARS:O:u}
#		.if empty($v)
#		.undef $v
#		.endif
#		.endfor
#		.endif
#		# make sure we know what TARGET_SPEC is
#		# as we may need it to find Makefile.depend*
#		TARGET_SPEC = ${TARGET_SPEC_VARS:@v@${$v:U}@:ts,}
#	
#	The following variables can influence the initial DIRDEPS
#	computation with regard to the TARGET_SPECs that will be
#	built.
#	Most should also be considered by init.mk
#	
#	ONLY_TARGET_SPEC_LIST
#		Defines a list of TARGET_SPECs for which the current
#		directory can be built.
#		If ALL_MACHINES is defined, we build for all the
#		TARGET_SPECs listed.
#
#	ONLY_MACHINE_LIST
#		As for ONLY_TARGET_SPEC_LIST but only specifies
#		MACHINEs.
#
#	NOT_TARGET_SPEC_LIST
#		A list of TARGET_SPECs for which the current
#		directory should not be built.
#
#	NOT_MACHINE_LIST
#		A list of MACHINEs the current directory should not be
#		built for.
#

.if !target(bootstrap) && (make(bootstrap) || \
	make(bootstrap-this) || \
	make(bootstrap-recurse) || \
	make(bootstrap-empty))
# disable most of below
.MAKE.LEVEL = 1
.endif

# touch this at your peril
_DIRDEP_USE_LEVEL?= 0
.if ${.MAKE.LEVEL} == ${_DIRDEP_USE_LEVEL}
# only the first instance is interested in all this

.if !target(_DIRDEP_USE)

# do some setup we only need once
_CURDIR ?= ${.CURDIR}
_OBJDIR ?= ${.OBJDIR}

now_utc = ${%s:L:gmtime}
.if !defined(start_utc)
start_utc := ${now_utc}
.endif

.if ${MAKEFILE:T} == ${.PARSEFILE} && empty(DIRDEPS) && ${.TARGETS:Uall:M*/*} != ""
# This little trick let's us do
#
# mk -f dirdeps.mk some/dir.${TARGET_SPEC}
#
all:
${.TARGETS:Nall}: all
DIRDEPS := ${.TARGETS:M*[/.]*}
# so that -DNO_DIRDEPS works
DEP_RELDIR := ${DIRDEPS:[1]:R}
# this will become DEP_MACHINE below
TARGET_MACHINE := ${DIRDEPS:[1]:E:C/,.*//}
.if ${TARGET_MACHINE:N*/*} == ""
TARGET_MACHINE := ${MACHINE}
.endif
# disable DIRDEPS_CACHE as it does not like this trick
MK_DIRDEPS_CACHE = no
.endif

# make sure we get the behavior we expect
.MAKE.SAVE_DOLLARS = no

# make sure these are empty to start with
_DEP_TARGET_SPEC =

# If TARGET_SPEC_VARS is other than just MACHINE
# it should be set by sys.mk or similar by now.
# TARGET_SPEC must not contain any '.'s.
TARGET_SPEC_VARS ?= MACHINE
# this is what we started with
TARGET_SPEC = ${TARGET_SPEC_VARS:@v@${$v:U}@:ts,}
# this is what we mostly use below
DEP_TARGET_SPEC = ${TARGET_SPEC_VARS:S,^,DEP_,:@v@${$v:U}@:ts,}
# make sure we have defaults
.for v in ${TARGET_SPEC_VARS}
DEP_$v ?= ${$v}
.endfor

.if ${TARGET_SPEC_VARS:[#]} > 1
# Ok, this gets more complex (putting it mildly).
# In order to stay sane, we need to ensure that all the build_dirs
# we compute below are fully qualified wrt DEP_TARGET_SPEC.
# The makefiles may only partially specify (eg. MACHINE only),
# so we need to construct a set of modifiers to fill in the gaps.
.if ${MAKE_VERSION} >= 20170130
_tspec_x := ${TARGET_SPEC_VARS:range}
.elif ${TARGET_SPEC_VARS:[#]} > 10
# seriously? better have jot(1) or equivalent to produce suitable sequence
_tspec_x := ${${JOT:Ujot} ${TARGET_SPEC_VARS:[#]}:L:sh}
.else
# we can provide the sequence ourselves
_tspec_x := ${1 2 3 4 5 6 7 8 9 10:L:[1..${TARGET_SPEC_VARS:[#]}]}
.endif
# this handles unqualified entries
M_dep_qual_fixes = C;(/[^/.,]+)$$;\1.$${DEP_TARGET_SPEC};
# there needs to be at least one item missing for these to make sense
.for i in ${_tspec_x:[2..-1]}
_tspec_m$i := ${TARGET_SPEC_VARS:[2..$i]:@w@[^,]+@:ts,}
_tspec_a$i := ,${TARGET_SPEC_VARS:[$i..-1]:@v@$$$${DEP_$v}@:ts,}
M_dep_qual_fixes += C;(\.${_tspec_m$i})$$;\1${_tspec_a$i};
.endfor
.else
# A harmless? default.
M_dep_qual_fixes = U
.endif

.if !defined(.MAKE.DEPENDFILE_PREFERENCE)
# .MAKE.DEPENDFILE_PREFERENCE makes the logic below neater?
# you really want this set by sys.mk or similar
.MAKE.DEPENDFILE_PREFERENCE = ${_CURDIR}/${.MAKE.DEPENDFILE:T}
.if ${.MAKE.DEPENDFILE:E} == "${TARGET_SPEC}"
.if ${TARGET_SPEC} != ${MACHINE}
.MAKE.DEPENDFILE_PREFERENCE += ${_CURDIR}/${.MAKE.DEPENDFILE:T:R}.$${MACHINE}
.endif
.MAKE.DEPENDFILE_PREFERENCE += ${_CURDIR}/${.MAKE.DEPENDFILE:T:R}
.endif
.endif

_default_dependfile := ${.MAKE.DEPENDFILE_PREFERENCE:[1]:T}
_machine_dependfiles := ${.MAKE.DEPENDFILE_PREFERENCE:T:M*${MACHINE}*}

# for machine specific dependfiles we require ${MACHINE} to be at the end
# also for the sake of sanity we require a common prefix
.if !defined(.MAKE.DEPENDFILE_PREFIX)
# knowing .MAKE.DEPENDFILE_PREFIX helps
.if !empty(_machine_dependfiles)
.MAKE.DEPENDFILE_PREFIX := ${_machine_dependfiles:[1]:T:R}
.else
.MAKE.DEPENDFILE_PREFIX := ${_default_dependfile:T}
.endif
.endif


# this is how we identify non-machine specific dependfiles
N_notmachine := ${.MAKE.DEPENDFILE_PREFERENCE:E:N*${MACHINE}*:${M_ListToSkip}}

.endif				# !target(_DIRDEP_USE)

# First off, we want to know what ${MACHINE} to build for.
# This can be complicated if we are using a mixture of ${MACHINE} specific
# and non-specific Makefile.depend*

# if we were included recursively _DEP_TARGET_SPEC should be valid.
.if empty(_DEP_TARGET_SPEC)
# we may or may not have included a dependfile yet
.if defined(.INCLUDEDFROMFILE)
_last_dependfile := ${.INCLUDEDFROMFILE:M${.MAKE.DEPENDFILE_PREFIX}*}
.else
_last_dependfile := ${.MAKE.MAKEFILES:M*/${.MAKE.DEPENDFILE_PREFIX}*:[-1]}
.endif
.if ${_debug_reldir:U0}
.info ${DEP_RELDIR}.${DEP_TARGET_SPEC}: _last_dependfile='${_last_dependfile}'
.endif

.if empty(_last_dependfile) || ${_last_dependfile:E:${N_notmachine}} == ""
# this is all we have to work with
DEP_MACHINE = ${TARGET_MACHINE:U${MACHINE}}
_DEP_TARGET_SPEC := ${DEP_TARGET_SPEC}
.else
_DEP_TARGET_SPEC = ${_last_dependfile:${M_dep_qual_fixes:ts:}:E}
.endif
.if !empty(_last_dependfile)
# record that we've read dependfile for this
_dirdeps_checked.${_CURDIR}.${TARGET_SPEC}:
.endif
.endif

# by now _DEP_TARGET_SPEC should be set, parse it.
.if ${TARGET_SPEC_VARS:[#]} > 1
# we need to parse DEP_MACHINE may or may not contain more info
_tspec := ${_DEP_TARGET_SPEC:S/,/ /g}
.for i in ${_tspec_x}
DEP_${TARGET_SPEC_VARS:[$i]} := ${_tspec:[$i]}
.endfor
.for v in ${TARGET_SPEC_VARS:O:u}
.if empty(DEP_$v)
.undef DEP_$v
.endif
.endfor
.else
DEP_MACHINE := ${_DEP_TARGET_SPEC}
.endif

# reset each time through
_build_all_dirs =

# the first time we are included the _DIRDEP_USE target will not be defined
# we can use this as a clue to do initialization and other one time things.
.if !target(_DIRDEP_USE)
# make sure this target exists
dirdeps: beforedirdeps .WAIT
beforedirdeps:

# We normally expect to be included by Makefile.depend.*
# which sets the DEP_* macros below.
DEP_RELDIR ?= ${RELDIR}

# this can cause lots of output!
# set to a set of glob expressions that might match RELDIR
DEBUG_DIRDEPS ?= no

# remember the initial value of DEP_RELDIR - we test for it below.
_DEP_RELDIR := ${DEP_RELDIR}

.endif

# DIRDEPS_CACHE can be very handy for debugging.
# Also if repeatedly building the same target,
# we can avoid the overhead of re-computing the tree dependencies.
MK_DIRDEPS_CACHE ?= no
BUILD_DIRDEPS_CACHE ?= no
BUILD_DIRDEPS ?= yes

.if ${MK_DIRDEPS_CACHE} == "yes"
# this is where we will cache all our work
DIRDEPS_CACHE ?= ${_OBJDIR:tA}/dirdeps.cache${.TARGETS:Nall:O:u:ts-:S,/,_,g:S,^,.,:N.}
.endif

# pickup customizations
# as below you can use !target(_DIRDEP_USE) to protect things
# which should only be done once.
.-include <local.dirdeps.mk>

.if !target(_DIRDEP_USE)
# things we skip for host tools
SKIP_HOSTDIR ?=

NSkipHostDir = ${SKIP_HOSTDIR:N*.host*:S,$,.host*,:N.host*:S,^,${SRCTOP}/,:${M_ListToSkip}}

# things we always skip
# SKIP_DIRDEPS allows for adding entries on command line.
SKIP_DIR += .host *.WAIT ${SKIP_DIRDEPS}
SKIP_DIR.host += ${SKIP_HOSTDIR}

DEP_SKIP_DIR = ${SKIP_DIR} \
	${SKIP_DIR.${DEP_TARGET_SPEC}:U} \
	${TARGET_SPEC_VARS:@v@${SKIP_DIR.${DEP_$v}:U}@} \
	${SKIP_DIRDEPS.${DEP_TARGET_SPEC}:U} \
	${TARGET_SPEC_VARS:@v@${SKIP_DIRDEPS.${DEP_$v}:U}@}


NSkipDir = ${DEP_SKIP_DIR:${M_ListToSkip}}

.if defined(NODIRDEPS) || defined(WITHOUT_DIRDEPS)
NO_DIRDEPS =
.elif defined(WITHOUT_DIRDEPS_BELOW)
NO_DIRDEPS_BELOW =
.endif

.if defined(NO_DIRDEPS)
# confine ourselves to the original dir and below.
DIRDEPS_FILTER += M${_DEP_RELDIR}*
.elif defined(NO_DIRDEPS_BELOW)
DIRDEPS_FILTER += M${_DEP_RELDIR}
.endif

# this is what we run below
DIRDEP_MAKE?= ${.MAKE}

# we suppress SUBDIR when visiting the leaves
# we assume sys.mk will set MACHINE_ARCH
# you can add extras to DIRDEP_USE_ENV
# if there is no makefile in the target directory, we skip it.
_DIRDEP_USE:	.USE .MAKE
	@for m in ${.MAKE.MAKEFILE_PREFERENCE}; do \
		test -s ${.TARGET:R}/$$m || continue; \
		echo "${TRACER}Checking ${.TARGET:R} for ${.TARGET:E} ..."; \
		MACHINE_ARCH= NO_SUBDIR=1 ${DIRDEP_USE_ENV} \
		TARGET_SPEC=${.TARGET:E} \
		MACHINE=${.TARGET:E} \
		${DIRDEP_MAKE} -C ${.TARGET:R} || exit 1; \
		break; \
	done

.ifdef ALL_MACHINES
# this is how you limit it to only the machines we have been built for
# previously.
.if empty(ONLY_TARGET_SPEC_LIST) && empty(ONLY_MACHINE_LIST)
.if !empty(ALL_MACHINE_LIST)
# ALL_MACHINE_LIST is the list of all legal machines - ignore anything else
_machine_list != cd ${_CURDIR} && 'ls' -1 ${ALL_MACHINE_LIST:O:u:@m@${.MAKE.DEPENDFILE:T:R}.$m@} 2> /dev/null; echo
.else
_machine_list != 'ls' -1 ${_CURDIR}/${.MAKE.DEPENDFILE_PREFIX}.* 2> /dev/null; echo
.endif
_only_machines := ${_machine_list:${NIgnoreFiles:UN*.bak}:E:O:u}
.else
_only_machines := ${ONLY_TARGET_SPEC_LIST:U} ${ONLY_MACHINE_LIST:U}
.endif

.if empty(_only_machines)
# we must be boot-strapping
_only_machines := ${TARGET_MACHINE:U${ALL_MACHINE_LIST:U${DEP_MACHINE}}}
.endif

.else				# ! ALL_MACHINES
# if ONLY_TARGET_SPEC_LIST or ONLY_MACHINE_LIST is set, we are limited to that.
# Note that ONLY_TARGET_SPEC_LIST should be fully qualified.
# if TARGET_MACHINE is set - it is really the same as ONLY_MACHINE_LIST
# otherwise DEP_MACHINE is it - so DEP_MACHINE will match.
_only_machines := ${ONLY_TARGET_SPEC_LIST:U:M${DEP_MACHINE},*}
.if empty(_only_machines)
_only_machines := ${ONLY_MACHINE_LIST:U${TARGET_MACHINE:U${DEP_MACHINE}}:M${DEP_MACHINE}}
.endif
.endif

.if !empty(NOT_MACHINE_LIST)
_only_machines := ${_only_machines:${NOT_MACHINE_LIST:${M_ListToSkip}}}
.endif
.if !empty(NOT_TARGET_SPEC_LIST)
# we must first qualify
_dm := ${DEP_MACHINE}
_only_machines := ${_only_machines:M*,*} ${_only_machines:N*,*:@DEP_MACHINE@${DEP_TARGET_SPEC}@:S,^,.,:${M_dep_qual_fixes:ts:}:O:u:S,^.,,}
DEP_MACHINE := ${_dm}
_only_machines := ${_only_machines:${NOT_TARGET_SPEC_LIST:${M_ListToSkip}}}
.endif
# clean up
_only_machines := ${_only_machines:O:u}

# make sure we have a starting place?
DIRDEPS ?= ${RELDIR}
.endif				# target

.if !defined(NO_DIRDEPS) && !defined(NO_DIRDEPS_BELOW)
.if ${MK_DIRDEPS_CACHE} == "yes"

# just ensure this exists
build-dirdeps:

M_oneperline = @x@\\${.newline}	$$x@

.if ${BUILD_DIRDEPS_CACHE} == "no"
.if !target(dirdeps-cached)
# we do this via sub-make
BUILD_DIRDEPS = no

# ignore anything but these
.MAKE.META.IGNORE_FILTER = M*/${.MAKE.DEPENDFILE_PREFIX}*

dirdeps: dirdeps-cached
dirdeps-cached:	${DIRDEPS_CACHE} .MAKE
	@echo "${TRACER}Using ${DIRDEPS_CACHE}"
	@MAKELEVEL=${.MAKE.LEVEL} ${.MAKE} -C ${_CURDIR} -f ${DIRDEPS_CACHE} \
		dirdeps MK_DIRDEPS_CACHE=no BUILD_DIRDEPS=no

# these should generally do
BUILD_DIRDEPS_MAKEFILE ?= ${MAKEFILE}
BUILD_DIRDEPS_TARGETS ?= ${.TARGETS}

# we need the .meta file to ensure we update if
# any of the Makefile.depend* changed.
# We do not want to compare the command line though.
${DIRDEPS_CACHE}:	.META .NOMETA_CMP
	+@{ echo '# Autogenerated - do NOT edit!'; echo; \
	echo 'BUILD_DIRDEPS=no'; echo; \
	echo '.include <dirdeps.mk>'; \
	} > ${.TARGET}.new
	+@MAKELEVEL=${.MAKE.LEVEL} DIRDEPS_CACHE=${DIRDEPS_CACHE} \
	DIRDEPS="${DIRDEPS}" \
	TARGET_SPEC=${TARGET_SPEC} \
	MAKEFLAGS= ${.MAKE} -C ${_CURDIR} -f ${BUILD_DIRDEPS_MAKEFILE} \
	${BUILD_DIRDEPS_TARGETS} BUILD_DIRDEPS_CACHE=yes \
	.MAKE.DEPENDFILE=.none \
	${.MAKEFLAGS:tW:S,-D ,-D,g:tw:M*WITH*} \
	3>&1 1>&2 | sed 's,${SRCTOP},$${SRCTOP},g' >> ${.TARGET}.new && \
	mv ${.TARGET}.new ${.TARGET}

.endif
.elif !target(_count_dirdeps)
# we want to capture the dirdeps count in the cache
.END: _count_dirdeps
_count_dirdeps: .NOMETA
	@echo '.info $${.newline}$${TRACER}Makefiles read: total=${.MAKE.MAKEFILES:[#]} depend=${.MAKE.MAKEFILES:M*depend*:[#]} dirdeps=${.ALLTARGETS:M${SRCTOP}*:O:u:[#]}' >&3

.endif
.elif !make(dirdeps) && !target(_count_dirdeps)
beforedirdeps: _count_dirdeps
_count_dirdeps: .NOMETA
	@echo "${TRACER}Makefiles read: total=${.MAKE.MAKEFILES:[#]} depend=${.MAKE.MAKEFILES:M*depend*:[#]} dirdeps=${.ALLTARGETS:M${SRCTOP}*:O:u:[#]} seconds=`expr ${now_utc} - ${start_utc}`"

.endif
.endif

.if ${BUILD_DIRDEPS} == "yes"
.if ${DEBUG_DIRDEPS:@x@${DEP_RELDIR:M$x}${${DEP_RELDIR}.${DEP_MACHINE}:L:M$x}@} != ""
_debug_reldir = 1
.else
_debug_reldir = 0
.endif
.if ${DEBUG_DIRDEPS:@x@${DEP_RELDIR:M$x}${${DEP_RELDIR}.depend:L:M$x}@} != ""
_debug_search = 1
.else
_debug_search = 0
.endif

# the rest is done repeatedly for every Makefile.depend we read.
# if we are anything but the original dir we care only about the
# machine type we were included for..

.if ${DEP_RELDIR} == "."
_this_dir := ${SRCTOP}
.else
_this_dir := ${SRCTOP}/${DEP_RELDIR}
.endif

# on rare occasions, there can be a need for extra help
_dep_hack := ${_this_dir}/${.MAKE.DEPENDFILE_PREFIX}.inc
.-include <${_dep_hack}>
.-include <${_dep_hack:R}.options>

.if ${DEP_RELDIR} != ${_DEP_RELDIR} || ${DEP_TARGET_SPEC} != ${TARGET_SPEC}
# this should be all
_machines := ${DEP_MACHINE}
.else
# this is the machine list we actually use below
_machines := ${_only_machines}

.if defined(HOSTPROG) || ${DEP_MACHINE:Nhost*} == ""
# we need to build this guy's dependencies for host as well.
.if ${DEP_MACHINE:Nhost*} == ""
_machines += ${DEP_MACHINE}
.else
_machines += host
.endif
.endif

_machines := ${_machines:O:u}
.endif

.if ${TARGET_SPEC_VARS:[#]} > 1
# we need to tweak _machines
_dm := ${DEP_MACHINE}
# apply the same filtering that we do when qualifying DIRDEPS.
# M_dep_qual_fixes expects .${MACHINE}* so add (and remove) '.'
# Again we expect that any already qualified machines are fully qualified.
_machines := ${_machines:M*,*} ${_machines:N*,*:@DEP_MACHINE@${DEP_TARGET_SPEC}@:S,^,.,:${M_dep_qual_fixes:ts:}:O:u:S,^.,,}
DEP_MACHINE := ${_dm}
_machines := ${_machines:O:u}
.endif

# reset each time through
_build_dirs =

.if ${DEP_RELDIR} == ${_DEP_RELDIR}
# pickup other machines for this dir if necessary
.if ${BUILD_AT_LEVEL0:Uyes} == "no"
_build_dirs += ${_machines:@m@${_CURDIR}.$m@}
.else
_build_dirs += ${_machines:N${DEP_TARGET_SPEC}:@m@${_CURDIR}.$m@}
.if ${DEP_TARGET_SPEC} == ${TARGET_SPEC}
# pickup local dependencies now
.if ${MAKE_VERSION} < 20160220
.-include <.depend>
.else
.dinclude <.depend>
.endif
.endif
.endif
.endif

.if ${_debug_reldir}
.info ${DEP_RELDIR}.${DEP_TARGET_SPEC}: DIRDEPS='${DIRDEPS}'
.info ${DEP_RELDIR}.${DEP_TARGET_SPEC}: _machines='${_machines}'
.endif

.if !empty(DIRDEPS)
# these we reset each time through as they can depend on DEP_MACHINE
DEP_DIRDEPS_FILTER = \
	${DIRDEPS_FILTER.${DEP_TARGET_SPEC}:U} \
	${TARGET_SPEC_VARS:@v@${DIRDEPS_FILTER.${DEP_$v}:U}@} \
	${DIRDEPS_FILTER:U}
.if empty(DEP_DIRDEPS_FILTER)
# something harmless
DEP_DIRDEPS_FILTER = U
.endif

# this is what we start with
__depdirs := ${DIRDEPS:${NSkipDir}:${DEP_DIRDEPS_FILTER:ts:}:C,//+,/,g:O:u:@d@${SRCTOP}/$d@}

# some entries may be qualified with .<machine>
# the :M*/*/*.* just tries to limit the dirs we check to likely ones.
# the ${d:E:M*/*} ensures we don't consider junos/usr.sbin/mgd
__qual_depdirs := ${__depdirs:M*/*/*.*:@d@${exists($d):?:${"${d:E:M*/*}":?:${exists(${d:R}):?$d:}}}@}
__unqual_depdirs := ${__depdirs:${__qual_depdirs:Uno:${M_ListToSkip}}}

.if ${DEP_RELDIR} == ${_DEP_RELDIR}
# if it was called out - we likely need it.
__hostdpadd := ${DPADD:U.:M${HOST_OBJTOP}/*:S,${HOST_OBJTOP}/,,:H:${NSkipDir}:${DIRDEPS_FILTER:ts:}:S,$,.host,:N.*:@d@${SRCTOP}/$d@} \
	${DPADD:U.:M${HOST_OBJTOP32:Uno}/*:S,${HOST_OBJTOP32:Uno}/,,:H:${NSkipDir}:${DIRDEPS_FILTER:ts:}:S,$,.host32,:N.*:@d@${SRCTOP}/$d@}
__qual_depdirs += ${__hostdpadd}
.endif

.if ${_debug_reldir}
.info depdirs=${__depdirs}
.info qualified=${__qual_depdirs}
.info unqualified=${__unqual_depdirs}
.endif

# _build_dirs is what we will feed to _DIRDEP_USE
_build_dirs += \
	${__qual_depdirs:M*.host:${NSkipHostDir}:N.host} \
	${__qual_depdirs:N*.host} \
	${_machines:Mhost*:@m@${__unqual_depdirs:@d@$d.$m@}@:${NSkipHostDir}:N.host} \
	${_machines:Nhost*:@m@${__unqual_depdirs:@d@$d.$m@}@}

# qualify everything now
_build_dirs := ${_build_dirs:${M_dep_qual_fixes:ts:}:O:u}

.endif				# empty DIRDEPS

_build_all_dirs += ${_build_dirs}
_build_all_dirs := ${_build_all_dirs:O:u}

# Normally if doing make -V something,
# we do not want to waste time chasing DIRDEPS
# but if we want to count the number of Makefile.depend* read, we do.
.if ${.MAKEFLAGS:M-V${_V_READ_DIRDEPS}} == ""
.if !empty(_build_all_dirs)
.if ${BUILD_DIRDEPS_CACHE} == "yes"
x!= { echo; echo '\# ${DEP_RELDIR}.${DEP_TARGET_SPEC}'; \
	echo 'dirdeps: ${_build_all_dirs:${M_oneperline}}'; echo; } >&3; echo
x!= { ${_build_all_dirs:@x@${target($x):?:echo '$x: _DIRDEP_USE';}@} echo; } >&3; echo
.if !empty(DEP_EXPORT_VARS)
# Discouraged, but there are always exceptions.
# Handle it here rather than explain how.
x!= { echo; ${DEP_EXPORT_VARS:@v@echo '$v=${$v}';@} echo '.export ${DEP_EXPORT_VARS}'; echo; } >&3; echo
.endif
.else
# this makes it all happen
dirdeps: ${_build_all_dirs}
.endif
${_build_all_dirs}:	_DIRDEP_USE

.if ${_debug_reldir}
.info ${DEP_RELDIR}.${DEP_TARGET_SPEC}: needs: ${_build_dirs}
.endif

.if !empty(DEP_EXPORT_VARS)
.export ${DEP_EXPORT_VARS}
DEP_EXPORT_VARS=
.endif

# this builds the dependency graph
.for m in ${_machines}
# it would be nice to do :N${.TARGET}
.if !empty(__qual_depdirs)
.for q in ${__qual_depdirs:${M_dep_qual_fixes:ts:}:E:O:u:N$m}
.if ${_debug_reldir} || ${DEBUG_DIRDEPS:@x@${${DEP_RELDIR}.$m:L:M$x}${${DEP_RELDIR}.$q:L:M$x}@} != ""
.info ${DEP_RELDIR}.$m: graph: ${_build_dirs:M*.$q}
.endif
.if ${BUILD_DIRDEPS_CACHE} == "yes"
x!= { echo; echo '${_this_dir}.$m: ${_build_dirs:M*.$q:${M_oneperline}}'; echo; } >&3; echo
.else
${_this_dir}.$m: ${_build_dirs:M*.$q}
.endif
.endfor
.endif
.if ${_debug_reldir}
.info ${DEP_RELDIR}.$m: graph: ${_build_dirs:M*.$m:N${_this_dir}.$m}
.endif
.if ${BUILD_DIRDEPS_CACHE} == "yes"
x!= { echo; echo '${_this_dir}.$m: ${_build_dirs:M*.$m:N${_this_dir}.$m:${M_oneperline}}'; echo; } >&3; echo
.else
${_this_dir}.$m: ${_build_dirs:M*.$m:N${_this_dir}.$m}
.endif
.endfor

.endif

# Now find more dependencies - and recurse.
.for d in ${_build_all_dirs}
.if !target(_dirdeps_checked.$d)
# once only
_dirdeps_checked.$d:
.if ${_debug_search}
.info checking $d
.endif
# Note: _build_all_dirs is fully qualifed so d:R is always the directory
.if exists(${d:R})
# we pass _DEP_TARGET_SPEC to tell the next step what we want
_DEP_TARGET_SPEC := ${d:E}
# some makefiles may still look at this
_DEP_MACHINE := ${d:E:C/,.*//}
# set these too in case Makefile.depend* uses them
.if ${TARGET_SPEC_VARS:[#]} > 1
_dtspec := ${_DEP_TARGET_SPEC:S/,/ /g}
.for i in ${_tspec_x}
DEP_${TARGET_SPEC_VARS:[$i]} := ${_dtspec:[$i]}
.endfor
.else
DEP_MACHINE := ${_DEP_MACHINE}
.endif
# Warning: there is an assumption here that MACHINE is always
# the first entry in TARGET_SPEC_VARS.
# If TARGET_SPEC and MACHINE are insufficient, you have a problem.
_m := ${.MAKE.DEPENDFILE_PREFERENCE:T:S;${TARGET_SPEC}$;${d:E};:S;${MACHINE};${d:E:C/,.*//};:@m@${exists(${d:R}/$m):?${d:R}/$m:}@:[1]}
.if !empty(_m)
# M_dep_qual_fixes isn't geared to Makefile.depend
_qm := ${_m:C;(\.depend)$;\1.${d:E};:${M_dep_qual_fixes:ts:}}
.if ${_debug_search}
.info Looking for ${_qm}
.endif
# set this "just in case"
# we can skip :tA since we computed the path above
DEP_RELDIR := ${_m:H:S,${SRCTOP}/,,}
# and reset this
DIRDEPS =
.if ${_debug_reldir} && ${_qm} != ${_m}
.info loading ${_m} for ${d:E}
.endif
.include <${_m}>
.else
.-include <local.dirdeps-missing.mk>
.endif
.endif
.endif
.endfor

.endif				# -V
.endif				# BUILD_DIRDEPS

.elif ${.MAKE.LEVEL} > 42
.error You should have stopped recursing by now.
.else
# we are building something
DEP_RELDIR := ${RELDIR}
_DEP_RELDIR := ${RELDIR}
# Since we are/should be included by .MAKE.DEPENDFILE
# This is a final opportunity to add/hook global rules.
.-include <local.dirdeps-build.mk>

# pickup local dependencies
.if ${MAKE_VERSION} < 20160220
.-include <.depend>
.else
.dinclude <.depend>
.endif
.endif

# bootstrapping new dependencies made easy?
.if !target(bootstrap) && (make(bootstrap) || \
	make(bootstrap-this) || \
	make(bootstrap-recurse) || \
	make(bootstrap-empty))

# if we are bootstrapping create the default
_want = ${.CURDIR}/${.MAKE.DEPENDFILE_DEFAULT:T}

.if exists(${_want})
# stop here
${.TARGETS:Mboot*}:
.elif !make(bootstrap-empty)
# find a Makefile.depend to use as _src
_src != cd ${.CURDIR} && for m in ${.MAKE.DEPENDFILE_PREFERENCE:T:S,${MACHINE},*,}; do test -s $$m || continue; echo $$m; break; done; echo
.if empty(_src)
.error cannot find any of ${.MAKE.DEPENDFILE_PREFERENCE:T}${.newline}Use: bootstrap-empty
.endif

_src?= ${.MAKE.DEPENDFILE}

.MAKE.DEPENDFILE_BOOTSTRAP_SED+= -e 's/${_src:E:C/,.*//}/${MACHINE}/g'

# just create Makefile.depend* for this dir
bootstrap-this:	.NOTMAIN
	@echo Bootstrapping ${RELDIR}/${_want:T} from ${_src:T}; \
	echo You need to build ${RELDIR} to correctly populate it.
.if ${_src:T} != ${.MAKE.DEPENDFILE_PREFIX:T}
	(cd ${.CURDIR} && sed ${.MAKE.DEPENDFILE_BOOTSTRAP_SED} ${_src} > ${_want:T})
.else
	cp ${.CURDIR}/${_src:T} ${_want}
.endif

# create Makefile.depend* for this dir and its dependencies
bootstrap: bootstrap-recurse
bootstrap-recurse: bootstrap-this

_mf := ${.PARSEFILE}
bootstrap-recurse:	.NOTMAIN .MAKE
	@cd ${SRCTOP} && \
	for d in `cd ${RELDIR} && ${.MAKE} -B -f ${"${.MAKEFLAGS:M-n}":?${_src}:${.MAKE.DEPENDFILE:T}} -V DIRDEPS`; do \
		test -d $$d || d=$${d%.*}; \
		test -d $$d || continue; \
		echo "Checking $$d for bootstrap ..."; \
		(cd $$d && ${.MAKE} -f ${_mf} bootstrap-recurse); \
	done

.endif

# create an empty Makefile.depend* to get the ball rolling.
bootstrap-empty: .NOTMAIN .NOMETA
	@echo Creating empty ${RELDIR}/${_want:T}; \
	echo You need to build ${RELDIR} to correctly populate it.
	@{ echo DIRDEPS=; echo ".include <dirdeps.mk>"; } > ${_want}

.endif
