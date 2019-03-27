# $FreeBSD$

.include <src.opts.mk>

PROG=	getconf

SRCS=	confstr.c getconf.c limits.c pathconf.c progenv.c sysconf.c \
	unsigned_limits.c
CFLAGS+= -I${.CURDIR}
CLEANFILES+=	confstr.c limits.c pathconf.c progenv.c sysconf.c \
		confstr.names limits.names pathconf.names sysconf.names \
		conflicting.names unique.names unsigned_limits.names

.SUFFIXES: .gperf .names
.PHONY: conflicts

all:	conflicts

FAKE_GPERF=	${.CURDIR}/fake-gperf.awk
.gperf.c: ${FAKE_GPERF}
	LC_ALL=C awk -f ${FAKE_GPERF} ${.IMPSRC} >${.TARGET}

.gperf.names:
	LC_ALL=C awk '/^[_A-Z]/ { print; }' ${.IMPSRC} | \
	    sed -e 's/,$$//' >${.TARGET}

conflicts: conflicting.names unique.names
	@if test `wc -l <conflicting.names` != `wc -l <unique.names`; then \
		echo "Name conflicts found!" >&2; \
		exit 1; \
	fi

# pathconf.names is not included here because pathconf names are
# syntactically distinct from the other kinds.
conflicting.names:	confstr.names limits.names sysconf.names
	cat ${.ALLSRC} >${.TARGET}

unique.names:		conflicting.names
	LC_ALL=C sort -u ${.ALLSRC} >${.TARGET}

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
