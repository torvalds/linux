#	$OpenBSD: Makefile.inc,v 1.66 2024/01/19 19:45:02 millert Exp $

# stdlib sources
.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/stdlib ${LIBCSRCDIR}/stdlib

SRCS+=	a64l.c abort.c atexit.c atoi.c atof.c atol.c atoll.c bsearch.c \
	exit.c ecvt.c gcvt.c getenv.c getopt_long.c \
	getsubopt.c hcreate.c heapsort.c imaxabs.c imaxdiv.c insque.c \
	l64a.c llabs.c lldiv.c lsearch.c malloc.c __mktemp4.c mkdtemp.c \
	mkstemp.c mktemp.c reallocarray.c merge.c posix_pty.c qsort.c \
	radixsort.c rand.c random.c realpath.c remque.c setenv.c strtoimax.c \
	strtol.c strtoll.c strtonum.c strtoul.c strtoull.c strtoumax.c \
	system.c \
	tfind.c thread_atexit.c tsearch.c \
	_rand48.c drand48.c erand48.c jrand48.c \
	lcong48.c lrand48.c mrand48.c nrand48.c seed48.c srand48.c \
	_Exit.c icdb.c

.if (${MACHINE_CPU} == "i386")
SRCS+=	abs.S div.S labs.S ldiv.S
.elif (${MACHINE_CPU} == "alpha")
# XXX should be .S's
SRCS+=	abs.c div.c labs.c ldiv.c
.else
SRCS+=	abs.c div.c labs.c ldiv.c
.endif

MAN+=	a64l.3 abort.3 abs.3 alloca.3 atexit.3 atof.3 atoi.3 atol.3 atoll.3 \
	bsearch.3 div.3 ecvt.3 exit.3 getenv.3 getopt.3 getopt_long.3 \
	getsubopt.3 hcreate.3 imaxabs.3 imaxdiv.3 insque.3 labs.3 ldiv.3 \
	lldiv.3 lsearch.3 malloc.3 mktemp.3 posix_memalign.3 posix_openpt.3 \
	ptsname.3 qsort.3 radixsort.3 rand48.3 rand.3 random.3 realpath.3 \
	strtod.3 strtonum.3 strtol.3 strtoul.3 system.3 tsearch.3
