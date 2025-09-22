# $OpenBSD: bsd.regress.mk,v 1.28 2024/11/26 06:12:44 tb Exp $
# Documented in bsd.regress.mk(5)

# No man pages for regression tests.
NOMAN=

# Include debug information in binaries
CFLAGS+= -g

# No installation.
install:

# If REGRESS_TARGETS is defined and PROG is not defined, set NOPROG
.if defined(REGRESS_TARGETS) && !defined(PROG) && !defined(PROGS)
NOPROG=
.endif

.include <bsd.prog.mk>

.MAIN: all
all: regress

# XXX - Need full path to REGRESS_LOG, otherwise there will be much pain.
REGRESS_LOG?=/dev/null
REGRESS_SKIP_TARGETS?=
REGRESS_SKIP_SLOW?=no
.if ${REGRESS_LOG} != "/dev/null"
REGRESS_FAIL_EARLY?=no
.endif
REGRESS_FAIL_EARLY?=yes

.if ! ${REGRESS_LOG:M/*}
ERRORS += "Fatal: REGRESS_LOG=${REGRESS_LOG} is not an absolute path"
.endif

_REGRESS_NAME=${.CURDIR:S/${BSDSRCDIR}\/regress\///}
_REGRESS_TMP?=/dev/null
_REGRESS_OUT= | tee -a ${REGRESS_LOG} ${_REGRESS_TMP} 2>&1 > /dev/null

.for p in ${PROG} ${PROGS}
run-regress-$p: $p
. if !commands(run-regress-$p)
	./$p
. endif
.PHONY: run-regress-$p
.endfor

.if (defined(PROG) || defined(PROGS)) && !defined(REGRESS_TARGETS)
REGRESS_TARGETS=	${PROG:S/^/run-regress-/} ${PROGS:S/^/run-regress-/}
.  if defined(REGRESS_SKIP)
REGRESS_SKIP_TARGETS=	${PROG:S/^/run-regress-/} ${PROGS:S/^/run-regress-/}
.  endif
.endif

.if defined(REGRESS_SLOW_TARGETS) && ${REGRESS_SKIP_SLOW:L} != no
REGRESS_SKIP_TARGETS+=${REGRESS_SLOW_TARGETS}
.endif

.if ${REGRESS_FAIL_EARLY:L} != no
_REGRESS_FAILED = false
.else
_REGRESS_FAILED = true
.endif

.if defined(REGRESS_ROOT_TARGETS)
_ROOTUSER!=id -g
SUDO?=
.  if (${_ROOTUSER} != 0) && empty(SUDO)
REGRESS_SKIP_TARGETS+=${REGRESS_ROOT_TARGETS}
.  endif
.endif

REGRESS_EXPECTED_FAILURES?=
REGRESS_SETUP?=
REGRESS_SETUP_ONCE?=
REGRESS_CLEANUP?=

.if !empty(REGRESS_SETUP)
${REGRESS_TARGETS}: ${REGRESS_SETUP}
.endif

.if !empty(REGRESS_SETUP_ONCE)
CLEANFILES+=${REGRESS_SETUP_ONCE:S/^/stamp-/}
${REGRESS_TARGETS}: ${REGRESS_SETUP_ONCE:S/^/stamp-/}
${REGRESS_SETUP_ONCE:S/^/stamp-/}: .SILENT
	echo '==== ${@:S/^stamp-//} ===='
	${MAKE} -C ${.CURDIR} ${@:S/^stamp-//}
	date >$@
	echo
.endif

regress: .SILENT
.if !empty(REGRESS_SETUP_ONCE)
	rm -f ${REGRESS_SETUP_ONCE:S/^/stamp-/}
	${MAKE} -C ${.CURDIR} ${REGRESS_SETUP_ONCE:S/^/stamp-/}
.endif
.for RT in ${REGRESS_TARGETS}
	echo '==== ${RT} ===='
.  if ${REGRESS_SKIP_TARGETS:M${RT}}
	echo -n "SKIP " ${_REGRESS_OUT}
	echo SKIPPED
.  elif ${REGRESS_EXPECTED_FAILURES:M${RT}}
	if ${MAKE} -C ${.CURDIR} ${RT}; then \
	    echo -n "XPASS " ${_REGRESS_OUT} ; \
	    echo UNEXPECTED_PASS; \
	    ${_REGRESS_FAILED}; \
	else \
	    echo -n "XFAIL " ${_REGRESS_OUT} ; \
	    echo EXPECTED_FAIL; \
	fi
.  else
	if ${MAKE} -C ${.CURDIR} ${RT}; then \
	    echo -n "SUCCESS " ${_REGRESS_OUT} ; \
	else \
	    echo -n "FAIL " ${_REGRESS_OUT} ; \
	    echo FAILED ; \
	    ${_REGRESS_FAILED}; \
	fi
.  endif
	echo ${_REGRESS_NAME}/${RT:S/^run-regress-//} ${_REGRESS_OUT}
	echo
.endfor
.for RT in ${REGRESS_CLEANUP}
	echo '==== ${RT} ===='
	${MAKE} -C ${.CURDIR} ${RT}
	echo
.endfor
	rm -f ${REGRESS_SETUP_ONCE:S/^/stamp-/}

.if defined(ERRORS)
.BEGIN:
.  for _m in ${ERRORS}
	@echo 1>&2 ${_m}
.  endfor
.  if !empty(ERRORS:M"Fatal\:*") || !empty(ERRORS:M'Fatal\:*')
	@exit 1
.  endif
.endif

.PHONY: regress
