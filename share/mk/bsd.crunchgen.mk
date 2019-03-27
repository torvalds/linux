#################################################################
#
# Generate crunched binaries using crunchgen(1).
#
# General notes:
#
# A number of Make variables are used to generate the crunchgen config file.
#
#  CRUNCH_SRCDIRS: lists directories to search for included programs
#  CRUNCH_PROGS:  lists programs to be included
#  CRUNCH_LIBS:  libraries to statically link with
#  CRUNCH_SHLIBS:  libraries to dynamically link with
#  CRUNCH_BUILDOPTS: generic build options to be added to every program
#  CRUNCH_BUILDTOOLS: lists programs that need build tools built in the
#       local architecture.
#
# Special options can be specified for individual programs
#  CRUNCH_SRCDIR_${P}: base source directory for program ${P}
#  CRUNCH_BUILDOPTS_${P}: additional build options for ${P}
#  CRUNCH_ALIAS_${P}: additional names to be used for ${P}
#
# By default, any name appearing in CRUNCH_PROGS or CRUNCH_ALIAS_${P}
# will be used to generate a hard link to the resulting binary.
# Specific links can be suppressed by setting
# CRUNCH_SUPPRESS_LINK_${NAME} to 1.
#
# If CRUNCH_GENERATE_LINKS is set to no, no links will be generated.
#

# $FreeBSD$

##################################################################
#  The following is pretty nearly a generic crunchgen-handling makefile
#

CONF=	${PROG}.conf
OUTMK=	${PROG}.mk
OUTC=	${PROG}.c
OUTPUTS=${OUTMK} ${OUTC} ${PROG}.cache
CRUNCHOBJS= ${.OBJDIR}
CRUNCH_GENERATE_LINKS?= yes
# Don't let the prog.mk use MK_AUTO_OBJ, but do let the component builds use
# it.
CRUNCHARGS+= MK_AUTO_OBJ=no
CRUNCH_BUILDOPTS+= MK_AUTO_OBJ=${MK_AUTO_OBJ}

CLEANFILES+= ${CONF} *.o *.lo *.c *.mk *.cache *.a *.h

# Don't try to extract debug info from ${PROG}.
MK_DEBUG_FILES= no

# Set a default SRCDIR for each for simpler handling below.
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
CRUNCH_SRCDIR_${P}?=	${.CURDIR}/../../${D}/${P}
.endfor
.endfor

# Program names and their aliases contribute hardlinks to 'rescue' executable,
# except for those that get suppressed.
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
${OUTPUTS}: ${CRUNCH_SRCDIR_${P}}/Makefile
.if ${CRUNCH_GENERATE_LINKS} == "yes"
.ifndef CRUNCH_SUPPRESS_LINK_${P}
LINKS+= ${BINDIR}/${PROG} ${BINDIR}/${P}
.endif
.for A in ${CRUNCH_ALIAS_${P}}
.ifndef CRUNCH_SUPPRESS_LINK_${A}
LINKS+= ${BINDIR}/${PROG} ${BINDIR}/${A}
.endif
.endfor
.endif
.endfor
.endfor

.if !defined(_SKIP_BUILD)
all: ${PROG}
.endif
exe: ${PROG}

${CONF}: Makefile
	echo \# Auto-generated, do not edit >${.TARGET}
.ifdef CRUNCH_BUILDOPTS
	echo buildopts ${CRUNCH_BUILDOPTS} >>${.TARGET}
.endif
.ifdef CRUNCH_LIBS
	echo libs ${CRUNCH_LIBS} >>${.TARGET}
.endif
.ifdef CRUNCH_SHLIBS
	echo libs_so ${CRUNCH_SHLIBS} >>${.TARGET}
.endif
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
	echo progs ${P} >>${.TARGET}
	echo special ${P} srcdir ${CRUNCH_SRCDIR_${P}} >>${.TARGET}
.ifdef CRUNCH_BUILDOPTS_${P}
	echo special ${P} buildopts DIRPRFX=${DIRPRFX}${P}/ \
	    ${CRUNCH_BUILDOPTS_${P}} >>${.TARGET}
.else
	echo special ${P} buildopts DIRPRFX=${DIRPRFX}${P}/ >>${.TARGET}
.endif
.ifdef CRUNCH_LIBS_${P}
	echo special ${P} lib ${CRUNCH_LIBS_${P}} >>${.TARGET}
.endif
.for A in ${CRUNCH_ALIAS_${P}}
	echo ln ${P} ${A} >>${.TARGET}
.endfor
.endfor
.endfor

CRUNCHGEN?= crunchgen
CRUNCHENV+= MK_TESTS=no \
	    UPDATE_DEPENDFILE=no \
	    _RECURSING_CRUNCH=1
.ORDER: ${OUTPUTS} objs
${OUTPUTS:[1]}: .META
${OUTPUTS:[2..-1]}: .NOMETA
${OUTPUTS}: ${CONF}
	MAKE="${MAKE}" ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} \
	    MK_AUTO_OBJ=${MK_AUTO_OBJ} \
	    ${CRUNCHGEN} -fq -m ${OUTMK} -c ${OUTC} ${CONF}
	# Avoid redundantly calling 'make objs' which we've done by our
	# own dependencies.
	sed -i '' \
	    -e "s/^\(${PROG}:.*\) \$$(SUBMAKE_TARGETS)/\1/" \
	    ${OUTMK}

# These 2 targets cannot use .MAKE since they depend on the generated
# ${OUTMK} above.
${PROG}: ${OUTPUTS} objs .NOMETA .PHONY
	${CRUNCHENV} \
	    CC="${CC} ${CFLAGS} ${LDFLAGS}" \
	    CXX="${CXX} ${CXXFLAGS} ${LDFLAGS}" \
	    ${MAKE} ${CRUNCHARGS} .MAKE.MODE="${.MAKE.MODE} curdirOk=yes" \
	    .MAKE.META.IGNORE_PATHS="${.MAKE.META.IGNORE_PATHS}" \
	    -f ${OUTMK} exe

objs: ${OUTMK} .META
	${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} \
	    ${MAKE} -f ${OUTMK} ${CRUNCHARGS} BUILD_TOOLS_META=.NOMETA objs

# <sigh> Someone should replace the bin/csh and bin/sh build-tools with
# shell scripts so we can remove this nonsense.
.for _tool in ${CRUNCH_BUILDTOOLS}
build-tools-${_tool}:
	${_+_}cd ${.CURDIR}/../../${_tool}; \
	    if [ "${MK_AUTO_OBJ}" = "no" ]; then \
	        ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} ${MAKE} obj; \
	    fi; \
	    ${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} ${MAKE} build-tools
build-tools: build-tools-${_tool}
.endfor

# Use a separate build tree to hold files compiled for this crunchgen binary
# Yes, this does seem to partly duplicate bsd.subdir.mk, but I can't
# get that to cooperate with bsd.prog.mk.  Besides, many of the standard
# targets should NOT be propagated into the components.
.if ${MK_AUTO_OBJ} == "no"
_obj=	obj
.endif
.for __target in clean cleandepend cleandir ${_obj} objlink
.for D in ${CRUNCH_SRCDIRS}
.for P in ${CRUNCH_PROGS_${D}}
${__target}_crunchdir_${P}: .PHONY .MAKE
	${_+_}cd ${CRUNCH_SRCDIR_${P}} && \
	    ${CRUNCHENV} MAKEOBJDIRPREFIX=${CANONICALOBJDIR} ${MAKE} \
	    ${CRUNCHARGS} \
	    DIRPRFX=${DIRPRFX}${P}/ ${CRUNCH_BUILDOPTS} \
	    ${CRUNCH_BUILDOPTS_${P}} ${__target}
${__target}: ${__target}_crunchdir_${P}
.endfor
.endfor
.endfor

clean:
	rm -f ${CLEANFILES}
	${_+_}if [ -e ${.OBJDIR}/${OUTMK} ]; then			\
		${CRUNCHENV} MAKEOBJDIRPREFIX=${CRUNCHOBJS} ${MAKE} 	\
		-f ${OUTMK} clean;					\
	fi

META_XTRAS+=	${find ${CRUNCHOBJS}${SRCTOP} -name '*.meta' 2>/dev/null || true:L:sh}
META_XTRAS+=	${echo ${CRUNCHOBJS}/*.lo.meta 2>/dev/null || true:L:sh}
META_XTRAS+=	${PROG}.meta
