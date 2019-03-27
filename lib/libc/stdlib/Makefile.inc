#	from @(#)Makefile.inc	8.3 (Berkeley) 2/4/95
# $FreeBSD$

# machine-independent stdlib sources
.PATH: ${LIBC_SRCTOP}/${LIBC_ARCH}/stdlib ${LIBC_SRCTOP}/stdlib

MISRCS+=C99_Exit.c a64l.c abort.c abs.c atexit.c atof.c atoi.c atol.c atoll.c \
	bsearch.c \
	cxa_thread_atexit.c cxa_thread_atexit_impl.c \
	div.c exit.c getenv.c getopt.c getopt_long.c \
	getsubopt.c hcreate.c hcreate_r.c hdestroy_r.c heapsort.c heapsort_b.c \
	hsearch_r.c imaxabs.c imaxdiv.c \
	insque.c l64a.c labs.c ldiv.c llabs.c lldiv.c lsearch.c \
	merge.c mergesort_b.c ptsname.c qsort.c qsort_r.c quick_exit.c \
	radixsort.c rand.c \
	random.c reallocarray.c reallocf.c realpath.c remque.c \
	set_constraint_handler_s.c strfmon.c strtoimax.c \
	strtol.c strtold.c strtoll.c strtoq.c strtoul.c strtonum.c strtoull.c \
	strtoumax.c strtouq.c system.c tdelete.c tfind.c tsearch.c twalk.c

# Work around an issue on case-insensitive file systems.
# libc has both _Exit.c and _exit.s and they both yield
# _exit.o (case insensitively speaking).
CLEANFILES+=C99_Exit.c
C99_Exit.c: ${LIBC_SRCTOP}/stdlib/_Exit.c .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

SYM_MAPS+= ${LIBC_SRCTOP}/stdlib/Symbol.map

# machine-dependent stdlib sources
.sinclude "${LIBC_SRCTOP}/${LIBC_ARCH}/stdlib/Makefile.inc"

MAN+=	a64l.3 abort.3 abs.3 alloca.3 atexit.3 atof.3 \
	atoi.3 atol.3 at_quick_exit.3 bsearch.3 \
	div.3 exit.3 getenv.3 getopt.3 getopt_long.3 getsubopt.3 \
	hcreate.3 imaxabs.3 imaxdiv.3 insque.3 labs.3 ldiv.3 llabs.3 lldiv.3 \
	lsearch.3 memory.3 ptsname.3 qsort.3 \
	quick_exit.3 \
	radixsort.3 rand.3 random.3 reallocarray.3 reallocf.3 realpath.3 \
	set_constraint_handler_s.3 \
	strfmon.3 strtod.3 strtol.3 strtonum.3 strtoul.3 system.3 \
	tsearch.3

MLINKS+=a64l.3 l64a.3 a64l.3 l64a_r.3
MLINKS+=atol.3 atoll.3
MLINKS+=exit.3 _Exit.3
MLINKS+=getenv.3 putenv.3 getenv.3 setenv.3 getenv.3 unsetenv.3
MLINKS+=getopt_long.3 getopt_long_only.3
MLINKS+=hcreate.3 hdestroy.3 hcreate.3 hsearch.3
MLINKS+=hcreate.3 hcreate_r.3 hcreate.3 hdestroy_r.3 hcreate.3 hsearch_r.3
MLINKS+=insque.3 remque.3
MLINKS+=lsearch.3 lfind.3
MLINKS+=ptsname.3 grantpt.3 ptsname.3 unlockpt.3
MLINKS+=qsort.3 heapsort.3 qsort.3 mergesort.3 qsort.3 qsort_r.3
MLINKS+=rand.3 rand_r.3 rand.3 srand.3 rand.3 sranddev.3
MLINKS+=random.3 initstate.3 random.3 setstate.3 random.3 srandom.3 \
	random.3 srandomdev.3
MLINKS+=radixsort.3 sradixsort.3
MLINKS+=set_constraint_handler_s.3 abort_handler_s.3
MLINKS+=set_constraint_handler_s.3 ignore_handler_s.3
MLINKS+=strfmon.3 strfmon_l.3
MLINKS+=strtod.3 strtof.3 strtod.3 strtold.3
MLINKS+=strtol.3 strtoll.3 strtol.3 strtoq.3 strtol.3 strtoimax.3
MLINKS+=strtoul.3 strtoull.3 strtoul.3 strtouq.3 strtoul.3 strtoumax.3
MLINKS+=tsearch.3 tdelete.3 tsearch.3 tfind.3 tsearch.3 twalk.3
