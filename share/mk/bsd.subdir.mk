#	from: @(#)bsd.subdir.mk	5.9 (Berkeley) 2/1/91
# $FreeBSD$
#
# The include file <bsd.subdir.mk> contains the default targets
# for building subdirectories.
#
# For all of the directories listed in the variable SUBDIRS, the
# specified directory will be visited and the target made. There is
# also a default target which allows the command "make subdir" where
# subdir is any directory listed in the variable SUBDIRS.
#
#
# +++ variables +++
#
# DISTRIBUTION	Name of distribution. [base]
#
# SUBDIR	A list of subdirectories that should be built as well.
#		Each of the targets will execute the same target in the
#		subdirectories. SUBDIR.yes is automatically appended
#		to this list.
#
# +++ targets +++
#
#	distribute:
# 		This is a variant of install, which will
# 		put the stuff into the right "distribution".
#
# 	See SUBDIR_TARGETS for list of targets that will recurse.
#
# 	Targets defined in STANDALONE_SUBDIR_TARGETS will always be ran
# 	with SUBDIR_PARALLEL and will not respect .WAIT or SUBDIR_DEPEND_
# 	values.
#
# 	SUBDIR_TARGETS and STANDALONE_SUBDIR_TARGETS can be appended to
# 	via make.conf or src.conf.
#

.if !target(__<bsd.subdir.mk>__)
__<bsd.subdir.mk>__:

.if ${MK_AUTO_OBJ} == "no"
_obj=	obj
.endif

SUBDIR_TARGETS+= \
		all all-man analyze buildconfig buildfiles buildincludes \
		checkdpadd clean cleandepend cleandir cleanilinks \
		cleanobj depend distribute files includes installconfig \
		installdirs \
		installfiles installincludes print-dir realinstall \
		maninstall manlint ${_obj} objlink tags \

# Described above.
STANDALONE_SUBDIR_TARGETS+= \
		all-man buildconfig buildfiles buildincludes check checkdpadd \
		clean cleandepend cleandir cleanilinks cleanobj files includes \
		installconfig installdirs installincludes installfiles print-dir \
		maninstall manlint obj objlink

# It is safe to install in parallel when staging.
.if defined(NO_ROOT) || !empty(SYSROOT)
STANDALONE_SUBDIR_TARGETS+= realinstall
.endif

.include <bsd.init.mk>

.if make(print-dir)
NEED_SUBDIR=	1
ECHODIR=	:
.SILENT:
.if ${RELDIR:U.} != "."
print-dir:	.PHONY
	@echo ${RELDIR}
.endif
.endif

.if ${MK_AUTO_OBJ} == "yes" && !target(obj)
obj: .PHONY
.endif

.if !defined(NEED_SUBDIR)
# .MAKE.DEPENDFILE==/dev/null is set by bsd.dep.mk to avoid reading
# Makefile.depend
.if ${.MAKE.LEVEL} == 0 && ${MK_DIRDEPS_BUILD} == "yes" && !empty(SUBDIR) && \
    ${.MAKE.DEPENDFILE} != "/dev/null"
.include <meta.subdir.mk>
# ignore this
_SUBDIR:
.endif
.endif

DISTRIBUTION?=	base
.if !target(distribute)
distribute: .MAKE
.for dist in ${DISTRIBUTION}
	${_+_}cd ${.CURDIR}; \
	    ${MAKE} install installconfig -DNO_SUBDIR DESTDIR=${DISTDIR}/${dist} SHARED=copies
.endfor
.endif
# Convenience targets to run 'build${target}' and 'install${target}' when
# calling 'make ${target}'.
.for __target in files includes
.if !target(${__target})
${__target}:	build${__target} install${__target}
.ORDER:		build${__target} install${__target}
.endif
.endfor

# Make 'install' supports a before and after target.  Actual install
# hooks are placed in 'realinstall'.
.if !target(install)
.for __stage in before real after
.if !target(${__stage}install)
${__stage}install:
.endif
.endfor
install:	beforeinstall realinstall afterinstall
.ORDER:		beforeinstall realinstall afterinstall
.endif
.ORDER: all install

# SUBDIR recursing may be disabled for MK_DIRDEPS_BUILD
.if !target(_SUBDIR)

.if defined(SUBDIR) || defined(SUBDIR.yes)
SUBDIR:=${SUBDIR} ${SUBDIR.yes}
SUBDIR:=${SUBDIR:u}
.endif

# Subdir code shared among 'make <subdir>', 'make <target>' and SUBDIR_PARALLEL.
_SUBDIR_SH=	\
		if test -d ${.CURDIR}/$${dir}.${MACHINE_ARCH}; then \
			dir=$${dir}.${MACHINE_ARCH}; \
		fi; \
		${ECHODIR} "===> ${DIRPRFX}$${dir} ($${target})"; \
		cd ${.CURDIR}/$${dir}; \
		${MAKE} $${target} DIRPRFX=${DIRPRFX}$${dir}/

# This is kept for compatibility only.  The normal handling of attaching to
# SUBDIR_TARGETS will create a target for each directory.
_SUBDIR: .USEBEFORE
.if defined(SUBDIR) && !empty(SUBDIR) && !defined(NO_SUBDIR)
	@${_+_}target=${.TARGET:realinstall=install}; \
	    for dir in ${SUBDIR:N.WAIT}; do ( ${_SUBDIR_SH} ); done
.endif

# Create 'make subdir' targets to run the real 'all' target.
.for __dir in ${SUBDIR:N.WAIT}
${__dir}: all_subdir_${DIRPRFX}${__dir} .PHONY
.endfor

.for __target in ${SUBDIR_TARGETS}
# Can ordering be skipped for this and SUBDIR_PARALLEL forced?
.if ${STANDALONE_SUBDIR_TARGETS:M${__target}}
_is_standalone_target=	1
_subdir_filter=	N.WAIT
.else
_is_standalone_target=	0
_subdir_filter=
.endif
__subdir_targets=
.for __dir in ${SUBDIR:${_subdir_filter}}
.if ${__dir} == .WAIT
__subdir_targets+= .WAIT
.else
__deps=
.if ${_is_standalone_target} == 0
.if defined(SUBDIR_PARALLEL)
# Apply SUBDIR_DEPEND dependencies for SUBDIR_PARALLEL.
.for __dep in ${SUBDIR_DEPEND_${__dir}}
__deps+= ${__target}_subdir_${DIRPRFX}${__dep}
.endfor
.else
# For non-parallel builds, directories depend on all targets before them.
__deps:= ${__subdir_targets}
.endif	# defined(SUBDIR_PARALLEL)
.endif	# ${_is_standalone_target} == 0
${__target}_subdir_${DIRPRFX}${__dir}: .PHONY .MAKE .SILENT ${__deps}
	@${_+_}target=${__target:realinstall=install}; \
	    dir=${__dir}; \
	    ${_SUBDIR_SH};
__subdir_targets+= ${__target}_subdir_${DIRPRFX}${__dir}
.endif	# ${__dir} == .WAIT
.endfor	# __dir in ${SUBDIR}

# Attach the subdir targets to the real target.
# Only recurse on directly-called targets.  I.e., don't recurse on dependencies
# such as 'install' becoming {before,real,after}install, just recurse
# 'install'.  Despite that, 'realinstall' is special due to ordering issues
# with 'afterinstall'.
.if !defined(NO_SUBDIR) && (make(${__target}) || \
    (${__target} == realinstall && make(install)))
${__target}: ${__subdir_targets} .PHONY
.endif	# make(${__target})
.endfor	# __target in ${SUBDIR_TARGETS}

.endif	# !target(_SUBDIR)

# Ensure all targets exist
.for __target in ${SUBDIR_TARGETS}
.if !target(${__target})
${__target}:
.endif
.endfor

.endif
