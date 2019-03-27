# $FreeBSD$
#
# You must include bsd.test.mk instead of this file from your Makefile.
#
# Logic to build and install ATF test programs; i.e. test programs linked
# against the ATF libraries.

.if !target(__<bsd.test.mk>__)
.error atf.test.mk cannot be included directly.
.endif

# List of C, C++ and shell test programs to build.
#
# Programs listed here are built using PROGS, PROGS_CXX and SCRIPTS,
# respectively, from bsd.prog.mk.  However, the build rules are tweaked to
# require the ATF libraries.
#
# Test programs registered in this manner are set to be installed into TESTSDIR
# (which should be overridden by the Makefile) and are not required to provide a
# manpage.
ATF_TESTS_C?=
ATF_TESTS_CXX?=
ATF_TESTS_SH?=
ATF_TESTS_KSH93?=

.if !empty(ATF_TESTS_C)
PROGS+= ${ATF_TESTS_C}
_TESTS+= ${ATF_TESTS_C}
.for _T in ${ATF_TESTS_C}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}.c
DPADD.${_T}+= ${LIBATF_C}
.if empty(LDFLAGS:M-static) && empty(LDFLAGS.${_T}:M-static)
LDADD.${_T}+= ${LDADD_atf_c}
.else
LDADD.${_T}+= ${LIBATF_C}
.endif
TEST_INTERFACE.${_T}= atf
.endfor
.endif

.if !empty(ATF_TESTS_CXX)
PROGS_CXX+= ${ATF_TESTS_CXX}
_TESTS+= ${ATF_TESTS_CXX}
.for _T in ${ATF_TESTS_CXX}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}${CXX_SUFFIX:U.cc}
DPADD.${_T}+= ${LIBATF_CXX} ${LIBATF_C}
.if empty(LDFLAGS:M-static) && empty(LDFLAGS.${_T}:M-static)
LDADD.${_T}+= ${LDADD_atf_cxx} ${LDADD_atf_c}
.else
LDADD.${_T}+= ${LIBATF_CXX} ${LIBATF_C}
.endif
TEST_INTERFACE.${_T}= atf
.endfor
.endif

.if !empty(ATF_TESTS_SH)
SCRIPTS+= ${ATF_TESTS_SH}
_TESTS+= ${ATF_TESTS_SH}
.for _T in ${ATF_TESTS_SH}
SCRIPTSDIR_${_T}= ${TESTSDIR}
TEST_INTERFACE.${_T}= atf
CLEANFILES+= ${_T} ${_T}.tmp
# TODO(jmmv): It seems to me that this SED and SRC functionality should
# exist in bsd.prog.mk along the support for SCRIPTS.  Move it there if
# this proves to be useful within the tests.
ATF_TESTS_SH_SED_${_T}?= # empty
ATF_TESTS_SH_SRC_${_T}?= ${_T}.sh
${_T}: ${ATF_TESTS_SH_SRC_${_T}}
	echo '#! /usr/libexec/atf-sh' > ${.TARGET}.tmp
.if empty(ATF_TESTS_SH_SED_${_T})
	cat ${.ALLSRC:N*Makefile*} >>${.TARGET}.tmp
.else
	cat ${.ALLSRC:N*Makefile*} \
	    | sed ${ATF_TESTS_SH_SED_${_T}} >>${.TARGET}.tmp
.endif
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
.endif

.if !empty(ATF_TESTS_KSH93)
SCRIPTS+= ${ATF_TESTS_KSH93}
_TESTS+= ${ATF_TESTS_KSH93}
.for _T in ${ATF_TESTS_KSH93}
SCRIPTSDIR_${_T}= ${TESTSDIR}
TEST_INTERFACE.${_T}= atf
TEST_METADATA.${_T}+= required_programs="ksh93"
CLEANFILES+= ${_T} ${_T}.tmp
# TODO(jmmv): It seems to me that this SED and SRC functionality should
# exist in bsd.prog.mk along the support for SCRIPTS.  Move it there if
# this proves to be useful within the tests.
ATF_TESTS_KSH93_SED_${_T}?= # empty
ATF_TESTS_KSH93_SRC_${_T}?= ${_T}.sh
${_T}: ${ATF_TESTS_KSH93_SRC_${_T}}
	echo '#! /usr/libexec/atf-sh -s/usr/local/bin/ksh93' > ${.TARGET}.tmp
.if empty(ATF_TESTS_KSH93_SED_${_T})
	cat ${.ALLSRC:N*Makefile*} >>${.TARGET}.tmp
.else
	cat ${.ALLSRC:N*Makefile*} \
	    | sed ${ATF_TESTS_KSH93_SED_${_T}} >>${.TARGET}.tmp
.endif
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
.endif
