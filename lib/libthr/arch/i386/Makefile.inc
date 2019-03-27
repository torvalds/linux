# $FreeBSD$

SRCS+=	_umtx_op_err.S

# With the current compiler and libthr code, using SSE in libthr
# does not provide enough performance improvement to outweigh
# the extra context switch cost.  This can measurably impact
# performance when the application also does not use enough SSE.
CFLAGS+=${CFLAGS_NO_SIMD}
