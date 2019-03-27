# $FreeBSD$
#
# Support Clang static analyzer on SRCS.
#
#
# +++ variables +++
#
# CLANG_ANALYZE_CHECKERS	Which checkers to run for all sources.
#
# CLANG_ANALYZE_CXX_CHECKERS	Which checkers to run for C++ sources.
#
# CLANG_ANALYZE_OUTPUT		Output format for generated files.
# 				text - don't generate extra files.
# 				html - generate html in obj.plist/ directories.
# 				plist - generate xml obj.plist files.
# 				See also:
# 				  contrib/llvm/tools/clang/include/clang/StaticAnalyzer/Core/Analyses.def
#
# CLANG_ANALYZE_OUTPUT_DIR	Sets which directory output set by
# 				CLANG_ANALYZE_OUTPUT is placed into.
#
# +++ targets +++
#
#	analyze:
#		Run the Clang static analyzer against all sources and present
#		output on stdout.

.if !target(__<bsd.clang-analyze.mk>__)
__<bsd.clang-analyze.mk>__:

.include <bsd.compiler.mk>

.if ${COMPILER_TYPE} != "clang" && (make(analyze) || make(*.clang-analyzer))
.error Clang static analyzer requires clang but found that compiler '${CC}' is ${COMPILER_TYPE}
.endif

CLANG_ANALYZE_OUTPUT?=	text
CLANG_ANALYZE_OUTPUT_DIR?=	clang-analyze
CLANG_ANALYZE_FLAGS+=	--analyze \
			-Xanalyzer -analyzer-output=${CLANG_ANALYZE_OUTPUT} \
			-o ${CLANG_ANALYZE_OUTPUT_DIR}

CLANG_ANALYZE_CHECKERS+=	core deadcode security unix
CLANG_ANALYZE_CXX_CHECKERS+=	cplusplus

.for checker in ${CLANG_ANALYZE_CHECKERS}
CLANG_ANALYZE_FLAGS+=	-Xanalyzer -analyzer-checker=${checker}
.endfor
CLANG_ANALYZE_CXX_FLAGS+=	${CLANG_ANALYZE_FLAGS}
.for checker in ${CLANG_ANALYZE_CXX_CHECKERS}
CLANG_ANALYZE_CXX_FLAGS+=	-Xanalyzer -analyzer-checker=${checker}
.endfor

.SUFFIXES: .c .cc .cpp .cxx .C .clang-analyzer

CLANG_ANALYZE_CFLAGS=	${CFLAGS:N-Wa,--fatal-warnings}
CLANG_ANALYZE_CXXFLAGS=	${CXXFLAGS:N-Wa,--fatal-warnings}

.c.clang-analyzer:
	${CC:N${CCACHE_BIN}} ${CLANG_ANALYZE_FLAGS} \
	    ${CLANG_ANALYZE_CFLAGS} \
	    ${.IMPSRC}
.cc.clang-analyzer .cpp.clang-analyzer .cxx.clang-analyzer .C.clang-analyzer:
	${CXX:N${CCACHE_BIN}} ${CLANG_ANALYZE_CXX_FLAGS} \
	    ${CLANG_ANALYZE_CXXFLAGS} \
	    ${.IMPSRC}

CLANG_ANALYZE_SRCS= \
	${SRCS:M*.[cC]} ${SRCS:M*.cc} \
	${SRCS:M*.cpp} ${SRCS:M*.cxx} \
	${DPSRCS:M*.[cC]} ${DPSRCS:M*.cc} \
	${DPSRCS:M*.cpp} ${DPSRCS:M*.cxx}
.if !empty(CLANG_ANALYZE_SRCS)
CLANG_ANALYZE_OBJS=	${CLANG_ANALYZE_SRCS:O:u:${OBJS_SRCS_FILTER:ts:}:S,$,.clang-analyzer,}
.NOPATH:	${CLANG_ANALYZE_OBJS}
.endif

# .depend files aren't relevant here since they reference obj.o rather than
# obj.clang-analyzer, so add in some guesses in case 'make depend' wasn't ran,
# for when directly building 'obj.clang-analyzer'.
.for __obj in ${CLANG_ANALYZE_OBJS}
${__obj}: ${OBJS_DEPEND_GUESS}
${__obj}: ${OBJS_DEPEND_GUESS.${__obj}}
.endfor

beforeanalyze: depend .PHONY
.if !defined(_RECURSING_PROGS) && !empty(CLANG_ANALYZE_SRCS) && \
    ${CLANG_ANALYZE_OUTPUT} != "text"
	mkdir -p ${CLANG_ANALYZE_OUTPUT_DIR}
.endif

.if !target(analyze)
analyze: beforeanalyze .WAIT ${CLANG_ANALYZE_OBJS}
.endif

.if exists(${CLANG_ANALYZE_OUTPUT_DIR})
CLEANDIRS+=	${CLANG_ANALYZE_OUTPUT_DIR}
.endif

.endif	# !target(__<bsd.clang-analyze.mk>__)
