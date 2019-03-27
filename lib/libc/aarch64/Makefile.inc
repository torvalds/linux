# $FreeBSD$
#
# Machine dependent definitions for the arm 64-bit architecture.
#

# Long double is quad precision
GDTOASRCS+=strtorQ.c
SRCS+=machdep_ldisQ.c
SYM_MAPS+=${LIBC_SRCTOP}/aarch64/Symbol.map
