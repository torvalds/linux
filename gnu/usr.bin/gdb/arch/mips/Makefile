# $FreeBSD$

#
# XXX Should set DEFAULT_BFD_VEC based on target.
#
.if !defined(GDB_CROSS_DEBUGGER)
LIBSRCS+= mipsfbsd-nat.c fbsd-threads.c
.endif
LIBSRCS+= solib.c solib-svr4.c
LIBSRCS+= mips-tdep.c mipsfbsd-tdep.c fbsd-proc.c

nm.h:
	echo '#include "mips/nm-fbsd.h"' > ${.TARGET}

tm.h:
	echo '#include "mips/tm-fbsd.h"' > ${.TARGET}
