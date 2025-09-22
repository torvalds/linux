#	$OpenBSD: Makefile.inc,v 1.14 2022/10/12 10:48:33 kn Exp $
#	$NetBSD: Makefile.inc,v 1.1 1996/09/30 16:34:59 ws Exp $

.if !defined(__stand_makefile_inc)
__stand_makefile_inc=1

S=$(.CURDIR)/../../../$(R)

.if !make(clean) && !make(cleandir) && !make(includes) && !make(libdep) && \
    !make(sadep) && !make(salibdir) && !make(obj) && !defined(NOMACHINE)
.BEGIN:
	@([ -h machine ] || ln -s $(S)/arch/$(MACHINE)/include machine)
.endif

#
EXTRACFLAGS=	-msoft-float
ENTRY?=	_start

INCLUDES+=	-I. -I$(.OBJDIR) -I$(.CURDIR)/.. -I$(S)/arch -I$(S)
INCLUDES+=	-I$(S)/lib/libsa
DEFS+=		-DSTANDALONE
CFLAGS+=	$(INCLUDES) $(DEFS) $(EXTRACFLAGS)
CFLAGS+=	-fno-stack-protector
CFLAGS+=	-fno-builtin -fno-pie
AFLAGS+=	-fno-pie
LDFLAGS?=	-X -N -Ttext $(RELOC) -e $(ENTRY)

cleandir:
	rm -rf lib machine

.endif
