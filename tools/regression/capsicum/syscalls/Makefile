# $FreeBSD$

SYSCALLS=	cap_fcntls_limit cap_getmode cap_ioctls_limit

CFLAGS=		-O2 -pipe -std=gnu99 -fstack-protector
CFLAGS+=	-Wsystem-headers -Werror -Wall -Wno-format-y2k -W -Wno-unused-parameter
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wreturn-type
CFLAGS+=	-Wcast-qual -Wwrite-strings -Wswitch -Wshadow -Wunused-parameter
CFLAGS+=	-Wcast-align -Wchar-subscripts -Winline -Wnested-externs -Wredundant-decls
CFLAGS+=	-Wold-style-definition -Wno-pointer-sign

all:	${SYSCALLS} ${SYSCALLS:=.t}

.for SYSCALL in ${SYSCALLS}

${SYSCALL}:	${SYSCALL}.c misc.c
	${CC} ${CFLAGS} ${@}.c misc.c -o $@

${SYSCALL}.t:	${SYSCALL}
	@printf "#!/bin/sh\n\n%s/%s\n" ${.CURDIR} ${@:.t=} > $@

.endfor

test:	all
	@prove -r ${.CURDIR}

clean:
	rm -f ${SYSCALLS} ${SYSCALLS:=.t}
