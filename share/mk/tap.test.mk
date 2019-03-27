# $FreeBSD$
#
# You must include bsd.test.mk instead of this file from your Makefile.
#
# Logic to build and install TAP-compliant test programs.
#
# This is provided to support existing tests in the FreeBSD source tree
# (particularly those coming from tools/regression/) that comply with the
# Test Anything Protocol.  It should not be used for new tests.

.if !target(__<bsd.test.mk>__)
.error tap.test.mk cannot be included directly.
.endif

# List of C, C++ and shell test programs to build.
#
# Programs listed here are built according to the semantics of bsd.prog.mk for
# PROGS, PROGS_CXX and SCRIPTS, respectively.
#
# Test programs registered in this manner are set to be installed into TESTSDIR
# (which should be overridden by the Makefile) and are not required to provide a
# manpage.
TAP_TESTS_C?=
TAP_TESTS_CXX?=
TAP_TESTS_PERL?=
TAP_TESTS_SH?=

# Perl interpreter to use for test programs written in this language.
TAP_PERL_INTERPRETER?=	${LOCALBASE}/bin/perl

.if !empty(TAP_TESTS_C)
PROGS+= ${TAP_TESTS_C}
_TESTS+= ${TAP_TESTS_C}
.for _T in ${TAP_TESTS_C}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}.c
TEST_INTERFACE.${_T}= tap
.endfor
.endif

.if !empty(TAP_TESTS_CXX)
PROGS_CXX+= ${TAP_TESTS_CXX}
_TESTS+= ${TAP_TESTS_CXX}
.for _T in ${TAP_TESTS_CXX}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}.cc
TEST_INTERFACE.${_T}= tap
.endfor
.endif

.if !empty(TAP_TESTS_PERL)
SCRIPTS+= ${TAP_TESTS_PERL}
_TESTS+= ${TAP_TESTS_PERL}
.for _T in ${TAP_TESTS_PERL}
SCRIPTSDIR_${_T}= ${TESTSDIR}
TEST_INTERFACE.${_T}= tap
TEST_METADATA.${_T}+= required_programs="${TAP_PERL_INTERPRETER}"
CLEANFILES+= ${_T} ${_T}.tmp
# TODO(jmmv): It seems to me that this SED and SRC functionality should
# exist in bsd.prog.mk along the support for SCRIPTS.  Move it there if
# this proves to be useful within the tests.
TAP_TESTS_PERL_SED_${_T}?= # empty
TAP_TESTS_PERL_SRC_${_T}?= ${_T}.pl
${_T}: ${TAP_TESTS_PERL_SRC_${_T}}
	{ \
	    echo '#! ${TAP_PERL_INTERPRETER}'; \
	    cat ${.ALLSRC:N*Makefile*} | sed ${TAP_TESTS_PERL_SED_${_T}}; \
	} >${.TARGET}.tmp
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
.endif

.if !empty(TAP_TESTS_SH)
SCRIPTS+= ${TAP_TESTS_SH}
_TESTS+= ${TAP_TESTS_SH}
.for _T in ${TAP_TESTS_SH}
SCRIPTSDIR_${_T}= ${TESTSDIR}
TEST_INTERFACE.${_T}= tap
CLEANFILES+= ${_T} ${_T}.tmp
# TODO(jmmv): It seems to me that this SED and SRC functionality should
# exist in bsd.prog.mk along the support for SCRIPTS.  Move it there if
# this proves to be useful within the tests.
TAP_TESTS_SH_SED_${_T}?= # empty
TAP_TESTS_SH_SRC_${_T}?= ${_T}.sh
${_T}: ${TAP_TESTS_SH_SRC_${_T}}
.if empty(TAP_TESTS_SH_SED_${_T})
	cat ${.ALLSRC} >${.TARGET}.tmp
.else
	cat ${.ALLSRC} | sed ${TAP_TESTS_SH_SED_${_T}} >${.TARGET}.tmp
.endif
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
.endif
