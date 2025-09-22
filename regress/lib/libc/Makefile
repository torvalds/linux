#	$OpenBSD: Makefile,v 1.62 2025/08/04 06:10:40 tb Exp $

SUBDIR+= _setjmp
SUBDIR+= alloca arc4random-fork atexit
SUBDIR+= basename
SUBDIR+= cephes cxa-atexit
SUBDIR+= db dirname
SUBDIR+= elf_aux_info
SUBDIR+= env explicit_bzero
SUBDIR+= ffs fmemopen fnmatch fpclassify fread
SUBDIR+= gcvt getaddrinfo getcap getopt getopt_long glob
SUBDIR+= hash
SUBDIR+= hsearch
SUBDIR+= ieeefp ifnameindex illumos
SUBDIR+= ldexp locale longjmp
SUBDIR+= malloc mkstemp modf
SUBDIR+= netdb
SUBDIR+= open_memstream orientation
SUBDIR+= popen printf
SUBDIR+= qsort
SUBDIR+= regex
SUBDIR+= setjmp setjmp-signal sigsetjmp sigthr sleep sprintf stdio
SUBDIR+= stdio_threading stpncpy strchr strerror strlcat strlcpy strnlen
SUBDIR+= strtod strtol strtonum sys
SUBDIR+= telldir time timingsafe
SUBDIR+= uuid
SUBDIR+= vis
SUBDIR+= wprintf

.if defined(REGRESS_FULL) || make(clean) || make(cleandir) || make(obj)
SUBDIR+= setjmp-fpu
.endif

.if exists(arch/${MACHINE_ARCH})
SUBDIR+= arch/${MACHINE_ARCH}
.endif

# XXX asr tests to not use bsd.regress.mk and works completly different
# SUBDIR+= asr

install:

.include <bsd.subdir.mk>
