# $FreeBSD$
#
# Machine dependent definitions for the RISC-V architecture.
#

# Long double is quad precision
GDTOASRCS+=strtorQ.c
SRCS+=machdep_ldisQ.c
SYM_MAPS+=${LIBC_SRCTOP}/riscv/Symbol.map
