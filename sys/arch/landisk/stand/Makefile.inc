#	$OpenBSD: Makefile.inc,v 1.4 2017/07/25 13:32:14 robert Exp $

.if !make(clean) && !make(cleandir) && !make(includes) && !make(libdep) && \
    !make(sadep) && !make(salibdir) && !make(obj)
.BEGIN:
	@([ X$(S) = X -o -h machine ] || ln -s $(S)/arch/landisk/include machine)
	@([ X$(S) = X -o -h sh ] || ln -s $(S)/arch/sh/include sh)
.endif

CFLAGS+=-Oz -fno-stack-protector -fno-builtin
CLEANFILES+=	machine sh
BINDIR=	/usr/mdec
MANSUBDIR=landisk
