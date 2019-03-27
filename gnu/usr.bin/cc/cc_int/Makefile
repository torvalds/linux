# $FreeBSD$

.include <bsd.own.mk>

.include "../Makefile.inc"
.include "../Makefile.ver"

.PATH: ../cc_tools ${GCCDIR} ${GCCDIR}/config/${GCC_CPU}

LIB=	backend
INTERNALLIB=

EXTRA_OBJS=
GGC=		ggc-page.o
host_hook_obj=	host-default.o

#
# Most lists copied verbatim from GCC Makefiles.
#
OBJS-common = \
 double-int.o tree-chrec.o tree-scalar-evolution.o tree-data-ref.o	   \
 tree-cfg.o tree-dfa.o tree-eh.o tree-ssa.o tree-optimize.o tree-gimple.o  \
 gimplify.o tree-pretty-print.o tree-into-ssa.o				   \
 tree-outof-ssa.o tree-ssa-ccp.o tree-vn.o tree-ssa-uncprop.o		   \
 tree-ssa-dce.o  tree-ssa-copy.o tree-nrv.o tree-ssa-copyrename.o	   \
 tree-ssa-pre.o tree-ssa-live.o tree-ssa-operands.o tree-ssa-alias.o       \
 tree-ssa-alias-warnings.o						   \
 tree-ssa-phiopt.o tree-ssa-forwprop.o tree-nested.o tree-ssa-dse.o	   \
 tree-ssa-dom.o domwalk.o tree-tailcall.o gimple-low.o tree-iterator.o	   \
 omp-low.o tree-phinodes.o tree-ssanames.o tree-sra.o tree-complex.o	   \
 tree-vect-generic.o tree-ssa-loop.o tree-ssa-loop-niter.o		   \
 tree-ssa-loop-manip.o tree-ssa-threadupdate.o tree-ssa-threadedge.o	   \
 tree-vectorizer.o tree-vect-analyze.o tree-vect-transform.o		   \
 tree-vect-patterns.o tree-ssa-loop-prefetch.o				   \
 tree-ssa-loop-ivcanon.o tree-ssa-propagate.o tree-ssa-address.o	   \
 tree-ssa-math-opts.o							   \
 tree-ssa-loop-ivopts.o tree-if-conv.o tree-ssa-loop-unswitch.o		   \
 alias.o bb-reorder.o bitmap.o builtins.o caller-save.o calls.o	  	   \
 cfg.o cfganal.o cfgbuild.o cfgcleanup.o cfglayout.o cfgloop.o		   \
 cfgloopanal.o cfgloopmanip.o loop-init.o loop-unswitch.o loop-unroll.o	   \
 cfgrtl.o combine.o conflict.o convert.o coverage.o cse.o cselib.o 	   \
 dbxout.o ddg.o tree-ssa-loop-ch.o loop-invariant.o tree-ssa-loop-im.o	   \
 debug.o df-core.o df-problems.o df-scan.o dfp.o diagnostic.o dojump.o     \
 dominance.o loop-doloop.o	   					   \
 dwarf2asm.o dwarf2out.o emit-rtl.o except.o explow.o loop-iv.o		   \
 expmed.o expr.o final.o flow.o fold-const.o function.o gcse.o		   \
 genrtl.o ggc-common.o global.o graph.o gtype-desc.o			   \
 haifa-sched.o hooks.o ifcvt.o insn-attrtab.o insn-emit.o insn-modes.o	   \
 insn-extract.o insn-opinit.o insn-output.o insn-peep.o insn-recog.o	   \
 integrate.o intl.o jump.o  langhooks.o lcm.o lists.o local-alloc.o  	   \
 mode-switching.o modulo-sched.o optabs.o options.o opts.o opts-common.o \
 params.o postreload.o postreload-gcse.o predict.o			   \
 insn-preds.o insn-automata.o pointer-set.o 			   	   \
 print-rtl.o print-tree.o profile.o value-prof.o var-tracking.o		   \
 real.o recog.o reg-stack.o regclass.o regmove.o regrename.o		   \
 reload.o reload1.o reorg.o resource.o rtl.o rtlanal.o rtl-error.o	   \
 sbitmap.o sched-deps.o sched-ebb.o sched-rgn.o sched-vis.o sdbout.o	   \
 see.o simplify-rtx.o sreal.o stmt.o stor-layout.o stringpool.o		   \
 struct-equiv.o targhooks.o timevar.o toplev.o tracer.o tree.o tree-dump.o \
 varasm.o varray.o vec.o version.o vmsdbgout.o xcoffout.o alloc-pool.o	   \
 et-forest.o cfghooks.o bt-load.o pretty-print.o $(GGC) web.o passes.o     \
 tree-profile.o rtlhooks.o cfgexpand.o lambda-mat.o    			   \
 lambda-trans.o	lambda-code.o tree-loop-linear.o tree-ssa-sink.o 	   \
 tree-vrp.o tree-stdarg.o tree-cfgcleanup.o tree-ssa-reassoc.o		   \
 tree-ssa-structalias.o tree-object-size.o 				   \
 rtl-factoring.o

OBJS-archive=	$(EXTRA_OBJS) $(host_hook_obj) tree-inline.o		   \
  cgraph.o cgraphunit.o tree-nomudflap.o ipa.o ipa-inline.o		   \
  ipa-utils.o ipa-reference.o ipa-pure-const.o ipa-type-escape.o	   \
  ipa-prop.o ipa-cp.o

OBJS-md=  ${GCC_CPU}.o
.if exists(${GCCDIR}/config/${GCC_CPU}/${GCC_CPU}-c.c)
OBJS-md+= ${GCC_CPU}-c.o
.endif

# Target specific, C specific object file
C_TARGET_OBJS=

# Language-specific object files for C and Objective C.
C_AND_OBJC_OBJS = attribs.o c-errors.o c-lex.o c-pragma.o c-decl.o c-typeck.o \
  c-convert.o c-aux-info.o c-common.o c-opts.o c-format.o c-semantics.o \
  c-incpath.o cppdefault.o c-ppoutput.o c-cppbuiltin.o prefix.o \
  c-objc-common.o c-dump.o c-pch.o c-parser.o $(C_TARGET_OBJS) \
  c-gimplify.o tree-mudflap.o c-pretty-print.o c-omp.o

# Language-specific object files for C.
C_OBJS = c-lang.o stub-objc.o $(C_AND_OBJC_OBJS)

# Put objC stub objects in library. objc frontend will override it
# Put dummy checksum object in library too. It will be overridden
# by each indidual front-end binary as needed.
OBJS-dummy=  stub-objc.o dummy-checksum.o

OBJS-all=  ${OBJS-common} ${OBJS-archive} ${OBJS-md} ${C_AND_OBJC_OBJS} \
	   ${OBJS-dummy}

SRCS+= ${OBJS-all:R:S/$/.c/g}

toplev.o:	toplev.c
	${CC} ${CFLAGS} -DTARGET_NAME=\"${GCC_TARGET}\" -c ${.IMPSRC}

# make sure we don't find .o's in ../cc_tools/
CLEANFILES+= ${OBJS-all}

.include <bsd.lib.mk>
