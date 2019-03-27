# $FreeBSD$

.include <src.opts.mk>

GREP_LIBZ=YES

.if ${MK_BSD_GREP} != "yes"
PROG=	grep
.else
PROG=	gnugrep
.endif
SRCS=	closeout.c dfa.c error.c exclude.c grep.c grepmat.c hard-locale.c \
	isdir.c kwset.c obstack.c quotearg.c savedir.c search.c xmalloc.c \
	xstrtoumax.c
CLEANFILES+=	gnugrep.1

CFLAGS+=-I${.CURDIR} -I${SYSROOT:U${DESTDIR}}/usr/include/gnu -DHAVE_CONFIG_H

.if ${MK_BSD_GREP} != "yes"
LINKS+=	${BINDIR}/grep ${BINDIR}/egrep \
	${BINDIR}/grep ${BINDIR}/fgrep
MLINKS=	grep.1 egrep.1 grep.1 fgrep.1
.endif

LIBADD=	gnuregex bz2

.if defined(GREP_LIBZ) && !empty(GREP_LIBZ)
LIBADD+=	z
CFLAGS+=-DHAVE_LIBZ=1
.endif

gnugrep.1: grep.1
	${CP} ${.ALLSRC} ${.TARGET}

check:	all
	@failed=0; total=0; \
	for tst in ${TESTS}; do \
		total=$$(($$total+1)); \
		if GREP=${.OBJDIR}/${PROG} srcdir=${.CURDIR}/tests \
		   ${.CURDIR}/tests/$$tst; then \
			echo "PASS: $$tst"; \
		else \
			failed=$$(($$failed+1)); \
			echo "FAIL: $$tst"; \
		fi; \
	done; \
	if [ "$$failed" -eq 0 ]; then \
		echo "All $$total tests passed"; \
	else \
		echo "$$failed of $$total tests failed"; \
	fi

TESTS=	warning.sh khadafy.sh spencer1.sh bre.sh ere.sh status.sh empty.sh \
	options.sh backref.sh file.sh

.include <bsd.prog.mk>
