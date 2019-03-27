# $FreeBSD$

.include <src.opts.mk>

CFLAGS+=	-I.

.include "../Makefile.inc"
# Some of the logic needed in here is defined in Makefile.hdrs as it is shared
# with gnu/lib.
.include "Makefile.hdrs"

CFLAGS+=	-g
CFLAGS+=	-DGENERATOR_FILE -DHAVE_CONFIG_H

# Override LIBIBERTY set by Makefile.inc, We use our own for
# build tools.
LIBIBERTY=	libiberty.a

.PATH:	${GCCDIR} ${GCCLIB}/libiberty

#-----------------------------------------------------------------------
# Determine content of variables used by the target/host config files

#
# The list of headers to go into tm.h
#
.for H in ${TARGET_INC}
.for D in ${GCCDIR}/config ${GCCDIR} ${.CURDIR}
.if exists($D/$H)
TARGET_INC_FILES+=     $D/$H
.endif
.endfor
.endfor

#
# gtyp includes.
#
srcdir=		${GCCDIR}
CPPLIB_H=	${GCCLIB}/libcpp/include/line-map.h \
		${GCCLIB}/libcpp/include/cpplib.h
SYMTAB_H=	${GCCLIB}/libcpp/include/symtab.h
CPP_ID_DATA_H=	${CPPLIB_H} ${GCCLIB}/libcpp/include/cpp-id-data.h
HASHTAB_H=	${GCCLIB}/include/hashtab.h
SPLAY_TREE_H=	${GCCLIB}/include/splay-tree.h
out_file=	${srcdir}/config/${GCC_CPU}/${GCC_CPU}.c
tm_file_list=	${TARGET_INC_FILES}
host_xm_file_list= ${.CURDIR}/auto-host.h ${GCCLIB}/include/ansidecl.h
GTFILES_SRCDIR=	${srcdir}

# Copied unchanged from gcc/Makefile.in
GTFILES = $(srcdir)/input.h $(srcdir)/coretypes.h \
  $(CPP_ID_DATA_H) $(host_xm_file_list) \
  $(tm_file_list) $(HASHTAB_H) $(SPLAY_TREE_H) $(srcdir)/bitmap.h \
  $(srcdir)/coverage.c $(srcdir)/rtl.h \
  $(srcdir)/optabs.h $(srcdir)/tree.h $(srcdir)/function.h $(srcdir)/libfuncs.h $(SYMTAB_H) \
  $(srcdir)/real.h $(srcdir)/varray.h $(srcdir)/insn-addr.h $(srcdir)/hwint.h \
  $(srcdir)/ipa-reference.h $(srcdir)/output.h \
  $(srcdir)/cselib.h $(srcdir)/basic-block.h  $(srcdir)/cgraph.h \
  $(srcdir)/c-common.h $(srcdir)/c-tree.h $(srcdir)/reload.h \
  $(srcdir)/alias.c $(srcdir)/bitmap.c $(srcdir)/cselib.c $(srcdir)/cgraph.c \
  $(srcdir)/ipa-prop.c $(srcdir)/ipa-cp.c $(srcdir)/ipa-inline.c \
  $(srcdir)/dbxout.c $(srcdir)/dwarf2out.c $(srcdir)/dwarf2asm.c \
  $(srcdir)/dojump.c $(srcdir)/tree-profile.c \
  $(srcdir)/emit-rtl.c $(srcdir)/except.c $(srcdir)/explow.c $(srcdir)/expr.c \
  $(srcdir)/function.c $(srcdir)/except.h \
  $(srcdir)/gcse.c $(srcdir)/integrate.c $(srcdir)/lists.c $(srcdir)/optabs.c \
  $(srcdir)/profile.c $(srcdir)/regclass.c \
  $(srcdir)/reg-stack.c $(srcdir)/cfglayout.c \
  $(srcdir)/sdbout.c $(srcdir)/stor-layout.c \
  $(srcdir)/stringpool.c $(srcdir)/tree.c $(srcdir)/varasm.c \
  $(srcdir)/tree-mudflap.c $(srcdir)/tree-flow.h \
  $(srcdir)/c-objc-common.c $(srcdir)/c-common.c $(srcdir)/c-parser.c \
  $(srcdir)/tree-ssanames.c $(srcdir)/tree-eh.c $(srcdir)/tree-ssa-address.c \
  $(srcdir)/tree-phinodes.c $(srcdir)/tree-cfg.c \
  $(srcdir)/tree-dfa.c $(srcdir)/tree-ssa-propagate.c \
  $(srcdir)/tree-iterator.c $(srcdir)/gimplify.c \
  $(srcdir)/tree-chrec.h $(srcdir)/tree-vect-generic.c \
  $(srcdir)/tree-ssa-operands.h $(srcdir)/tree-ssa-operands.c \
  $(srcdir)/tree-profile.c $(srcdir)/tree-nested.c \
  $(srcdir)/ipa-reference.c $(srcdir)/tree-ssa-structalias.h \
  $(srcdir)/tree-ssa-structalias.c \
  $(srcdir)/c-pragma.h $(srcdir)/omp-low.c \
  $(srcdir)/targhooks.c $(srcdir)/cgraphunit.c $(out_file) \

# The list of frontend directories to look into
GTFILES_LANG_DIR_NAMES=

.if ${MK_CXX} != "no"
GTFILES_LANG_DIR_NAMES+=	cp
.endif

# The list of language specific files for gengtype
.for L in ${GTFILES_LANG_DIR_NAMES} c
.if exists(${GCCDIR}/$L-config-lang.in)
# Source the language config file
L_GTFILES!=	sh -c '. ${GCCDIR}/$L-config-lang.in; echo $$gtfiles'
.else
L_GTFILES!=	sh -c '. ${GCCDIR}/$L/config-lang.in; echo $$gtfiles'
.endif
.for F in ${L_GTFILES}
GTFILES_FILES+=	$F
GTFILES_LANGS+= $L
.endfor
.endfor
GTFILES+=	${GTFILES_FILES}

#
# Tree definition files.
#
TREE_DEF_FILES=

.if ${MK_CXX} != "no"
TREE_DEF_FILES+=	cp/cp-tree.def
.endif

#-----------------------------------------------------------------------
# Build rules for header files and generator tools

# Host config
config.h:
	TARGET_CPU_DEFAULT="${TARGET_CPU_DEFAULT}" \
	HEADERS="auto-host.h ansidecl.h" \
	DEFINES="" \
	/bin/sh ${GCCDIR}/mkconfig.sh ${.TARGET}

GENSRCS+=	config.h
CLEANFILES+=	cs-config.h

# Build config
bconfig.h:
	TARGET_CPU_DEFAULT="${TARGET_CPU_DEFAULT}" \
	HEADERS="auto-host.h ansidecl.h" \
	DEFINES="" \
	/bin/sh ${GCCDIR}/mkconfig.sh ${.TARGET}
.if exists(${GCCDIR}/config/${GCC_CPU}/${GCC_CPU}-modes.def)
	echo '#define EXTRA_MODES_FILE "${GCC_CPU}/${GCC_CPU}-modes.def"' >> ${.TARGET}
.endif

GENSRCS+=	bconfig.h
CLEANFILES+=	cs-bconfig.h

# tconfig.h
GENSRCS+=	tconfig.h
CLEANFILES+=	cs-tconfig.h

# Options
GENONLY+=	optionlist options.h options.c

# Target machine config
GENSRCS+=	tm.h
CLEANFILES+=	cs-tm.h

# Target machine protos/preds.
tm_p.h:
	TARGET_CPU_DEFAULT="${TARGET_CPU_DEFAULT}" \
	HEADERS="${GCC_CPU}/${GCC_CPU}-protos.h tm-preds.h" \
	DEFINES="" \
	/bin/sh ${GCCDIR}/mkconfig.sh tm_p.h

GENSRCS+=	tm_p.h
CLEANFILES+=	cs-tm_p.h

# gencheck
gencheck.h: ${TREE_DEF_FILES}
.for F in ${TREE_DEF_FILES}
	echo "#include \"$F\""					>> ${.TARGET}
.endfor
	touch ${.TARGET}

GENSRCS+=	gencheck.h


# Source header for gtyp generator.
gtyp-gen.h:	${GTFILES}
	echo "/* This file is machine generated.  Do not edit.  */" > ${.TARGET}
	echo "static const char * const srcdir = "		>> ${.TARGET}
	echo "\"$(GTFILES_SRCDIR)\";"				>> ${.TARGET}
	echo "static const char * const lang_files[] = {"	>> ${.TARGET}
.for F in ${GTFILES_FILES}
	echo "\"$F\", "						>> ${.TARGET}
.endfor
	echo "NULL};"						>> ${.TARGET}
	echo "static const char * const langs_for_lang_files[] = {">> ${.TARGET}
.for F in ${GTFILES_LANGS}
	echo "\"$F\", "						>> ${.TARGET}
.endfor
	echo "NULL};"						>> ${.TARGET}
	echo "static const char * const all_files[] = {"	>> ${.TARGET}
.for F in ${GTFILES}
	echo "\"$F\", "						>> ${.TARGET}
.endfor
	echo "NULL};"						>> ${.TARGET}
	echo "static const char * const lang_dir_names[] = {"	>> ${.TARGET}
.for F in c ${GTFILES_LANG_DIR_NAMES}
	echo "\"$F\", "						>> ${.TARGET}
.endfor
	echo "NULL};"						>> ${.TARGET}

GENSRCS+=	gtyp-gen.h

# Version header for gcov
GENSRCS+=	gcov-iov.h

# Multilib config file
multilib.h:
.if ${TARGET_ARCH} == "powerpc64" || ${TARGET_ARCH} == "amd64"
	echo 'static const char *const multilib_raw[] = { \
	    ". !m64 !m32;", \
	    "64:../lib m64 !m32;", \
	    "32:../lib32 !m64 m32;", NULL };'			> ${.TARGET}
	echo 'static const char *multilib_options = "m64/m32";'	>> ${.TARGET}
	echo 'static const char *const multilib_matches_raw[] = { \
	    "m64 m64;", "m32 m32;", NULL };'			>> ${.TARGET}
.else
	echo 'static const char *const multilib_raw[] = { \
	    ". ;", NULL };'					> ${.TARGET}
	echo 'static const char *multilib_options = "";'	>> ${.TARGET}
	echo 'static const char *const multilib_matches_raw[] = { \
	    NULL };'						>> ${.TARGET}
.endif
	echo 'static const char *multilib_extra = "";'		>> ${.TARGET}
	echo 'static const char *const multilib_exclusions_raw[] = { \
	    NULL };'						>> ${.TARGET}

GENSRCS+=	multilib.h

configargs.h:
	echo 'static const char configuration_arguments[] ='	> ${.TARGET}
	echo '	"FreeBSD/${TARGET_ARCH} system compiler";'	>> ${.TARGET}
	echo 'static const char thread_model[] = "posix";'	>> ${.TARGET}
	echo 'static const struct {'				>> ${.TARGET}
	echo '	const char *name, *value;'			>> ${.TARGET}
	echo '} configure_default_options[] = {'		>> ${.TARGET}
	echo '	{ "NULL", "NULL" } };'				>> ${.TARGET}

GENSRCS+=	configargs.h

# Language spec files
specs.h:
	echo '#include "cp/lang-specs.h"'			> ${.TARGET}

GENSRCS+=	specs.h

gstdint.h:
	echo '#include "sys/types.h"'				> ${.TARGET}
	echo '#include "sys/stdint.h"'				>> ${.TARGET}

GENSRCS+=	gstdint.h

# Linked headers
GENSRCS+=	gthr-default.h

GENSRCS+=	unwind.h

#
# gtype gunk
#
gengtype-lex.c:	gengtype-lex.l
	flex -ogengtype-lex.c ${.ALLSRC}

gengtype-yacc.h: gengtype-yacc.y
	yacc -d -o gengtype-yacc.c ${.ALLSRC}

gengtype-yacc.c: gengtype-yacc.h .NOMETA

gengtype-yacc+%DIKED.c: gengtype-yacc.c
	cat    ${.ALLSRC} > ${.TARGET}
	sed -e "s/xmalloc/malloc/g" \
	    -e "s/xrealloc/realloc/g" \
	    -e "s/malloc/xmalloc/g" \
	    -e "s/realloc/xrealloc/g" \
	    ${.ALLSRC} > ${.TARGET}

GENSRCS+= gengtype-lex.c gengtype-yacc.h gengtype-yacc+%DIKED.c
CLEANFILES+= gengtype-yacc.c

gengtype: gengtype.o gengtype-yacc+%DIKED.o gengtype-lex.o errors.o \
	  ${LIBIBERTY}
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC}

gtype-desc.h:	gengtype
	${BTOOLSPATH:U.}/gengtype
	touch ${.TARGET}

gtype-desc.c:	gtype-desc.h

GENONLY+=	gtype-desc.c gtype-desc.h
CLEANFILES+=	gt-*.h gtype-*.h

#
# Generator tools.
#
.for F in check checksum genrtl modes
gen$F:	gen$F.o errors.o ${LIBIBERTY}
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC}
.endfor

.for F in attr attrtab automata codes conditions config constants emit \
	extract flags  opinit output peep preds recog
gen$F:	gen$F.o rtl.o read-rtl.o ggc-none.o vec.o min-insn-modes.o \
	gensupport.o print-rtl.o errors.o ${LIBIBERTY}
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC} -lm
.endfor

gencondmd:	gencondmd.o
	${CC:N${CCACHE_BIN}} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${.ALLSRC}

#
# Generated .md files.
#
insn-conditions.md:	gencondmd
	${BTOOLSPATH:U.}/gencondmd > ${.TARGET}
GENSRCS+=	insn-conditions.md

#
# Generated header files.
#

.for F in constants
insn-$F.h:	gen$F ${MD_FILE}
	${BTOOLSPATH:U.}/gen$F ${MD_FILE} > ${.TARGET}
GENSRCS+=	insn-$F.h
.endfor

.for F in attr codes config flags
insn-$F.h:	gen$F ${MD_FILE} insn-conditions.md
	${BTOOLSPATH:U.}/gen$F ${MD_FILE} insn-conditions.md > ${.TARGET}
GENSRCS+=	insn-$F.h
.endfor

# Header files with irregular names.
genrtl.h:	gengenrtl
	${BTOOLSPATH:U.}/gengenrtl -h > ${.TARGET}
GENSRCS+=	genrtl.h

tm-preds.h:	genpreds
	${BTOOLSPATH:U.}/genpreds -h ${MD_FILE} > ${.TARGET}
GENSRCS+=	tm-preds.h

tm-constrs.h:	genpreds
	${BTOOLSPATH:U.}/genpreds -c ${MD_FILE} > ${.TARGET}
GENSRCS+=	tm-constrs.h

tree-check.h:	gencheck
	${BTOOLSPATH:U.}/gencheck > ${.TARGET}
GENSRCS+=	tree-check.h

insn-modes.h:	genmodes
	${BTOOLSPATH:U.}/genmodes -h > ${.TARGET}
GENSRCS+=	insn-modes.h

#
# Generated source files.
#
.for F in attrtab automata emit extract opinit output peep preds recog
insn-$F.c:	gen$F ${MD_FILE} insn-conditions.md
	${BTOOLSPATH:U.}/gen$F ${MD_FILE} insn-conditions.md > ${.TARGET}
GENONLY+=	insn-$F.c
.endfor

.for F in conditions
insn-$F.c:	gen$F ${MD_FILE}
	${BTOOLSPATH:U.}/gen$F ${MD_FILE} > ${.TARGET}
GENSRCS+=	insn-$F.c
.endfor

# Source files with irregular names.
insn-modes.c:	genmodes
	${BTOOLSPATH:U.}/genmodes > ${.TARGET}
GENONLY+=	insn-modes.c

min-insn-modes.c:	genmodes
	${BTOOLSPATH:U.}/genmodes -m > ${.TARGET}
GENSRCS+=	min-insn-modes.c

genrtl.c:	gengenrtl
	${BTOOLSPATH:U.}/gengenrtl > ${.TARGET}
GENONLY+=	genrtl.c

gencondmd.c:	genconditions ${MD_FILE}
	${BTOOLSPATH:U.}/genconditions ${MD_FILE} > ${.TARGET}
GENSRCS+=	gencondmd.c

#-----------------------------------------------------------------------
# Build tools.

GNTOOLS+=	genattr genattrtab genautomata gencodes gencheck genchecksum \
		genconditions gencondmd genconfig genconstants genemit \
		genextract genflags gengenrtl gengtype genmodes genopinit \
		genoutput genpeep genpreds genrecog

${GNTOOLS:C,$,.o,} ${GNTOOLS}: ${BUILD_TOOLS_META}
all: ${GNTOOLS} ${GENSRCS} ${GENONLY}
build-tools: all
beforedepend: ${GENONLY}

#
#-----------------------------------------------------------------------
# Build 'pocket' libiberty exclusively for build tools use.

LIBIBERTY_SRCS=	choose-temp.c concat.c cp-demangle.c cp-demint.c cplus-dem.c \
	dyn-string.c fibheap.c fopen_unlocked.c getpwd.c getruntime.c \
	hashtab.c hex.c lbasename.c make-temp-file.c md5.c obstack.c \
	partition.c pex-unix.c physmem.c safe-ctype.c splay-tree.c xexit.c \
	xmalloc.c xmemdup.c xstrdup.c xstrerror.c
LIBIBERTY_OBJS=	${LIBIBERTY_SRCS:R:S/$/.o/g}

.for _src in ${LIBIBERTY_SRCS}
${_src:R:S/$/.o/}: ${_src} ${BUILD_TOOLS_META}
	${CC} -c -I ${.CURDIR}/../libiberty ${CFLAGS} -o ${.TARGET} ${.IMPSRC}
.endfor

${LIBIBERTY}: ${LIBIBERTY_OBJS} ${BUILD_TOOLS_META}
	@rm -f ${.TARGET}
	@${AR} ${ARFLAGS} ${.TARGET} `lorder ${LIBIBERTY_OBJS} | tsort -q`
	${RANLIB} ${.TARGET}
CLEANFILES+=	${LIBIBERTY} ${LIBIBERTY_OBJS}


#-----------------------------------------------------------------------
# Fixups.

# Set OBJS the same as bsd.prog.mk would do if we defined PROG.  We can't
# define PROG because we have multiple programs.
#
SRCS=		errors.c genattr.c genattrtab.c \
		genautomata.c gencheck.c genchecksum.c gencodes.c \
		genconditions.c genconfig.c genconstants.c genemit.c \
		genextract.c genflags.c gengenrtl.c gengtype.c genmodes.c \
		genopinit.c genoutput.c genpeep.c genpreds.c genrecog.c \
		gensupport.c ggc-none.c print-rtl.c read-rtl.c rtl.c \
		vec.c

SRCS+=		${GENSRCS}
OBJS+=		${SRCS:N*.h:R:S/$/.o/g}
GENOBJS+=	${GENSRCS:N*.h:R:S/$/.o/g}
${OBJS} ${GENOBJS}: ${BUILD_TOOLS_META}
CLEANFILES+=	${GENSRCS} ${GENONLY} ${GENOBJS} ${GNTOOLS}

#-----------------------------------------------------------------------
# Manual dependencies.
.include  "Makefile.dep"

.include <bsd.prog.mk>
# DO NOT DELETE
