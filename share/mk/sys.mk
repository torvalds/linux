#	$OpenBSD: sys.mk,v 1.98 2025/09/10 16:00:04 deraadt Exp $
#	$NetBSD: sys.mk,v 1.27 1996/04/10 05:47:19 mycroft Exp $
#	@(#)sys.mk	5.11 (Berkeley) 3/13/91

.if defined(EXTRA_SYS_MK_INCLUDES)
.for __SYS_MK_INCLUDE in ${EXTRA_SYS_MK_INCLUDES}
.include ${__SYS_MK_INCLUDE}
.endfor
.endif

unix=		We run OpenBSD.
OSMAJOR=	7
OSMINOR=	8
OSREV=		$(OSMAJOR).$(OSMINOR)
OSrev=		$(OSMAJOR)$(OSMINOR)

.SUFFIXES: .out .a .o .c .cc .C .cxx .cpp .F .f .r .y .l .s .S .cl .p .h .sh .m4

AR?=		ar
ARFLAGS?=	r
RANLIB?=	ranlib
LORDER?=	lorder

AS?=		as
AFLAGS?=	${DEBUG}
COMPILE.s?=	${CC} ${AFLAGS} -c
LINK.s?=	${CC} ${AFLAGS} ${LDFLAGS}
COMPILE.S?=	${CC} ${AFLAGS} ${CPPFLAGS} -c
LINK.S?=	${CC} ${AFLAGS} ${CPPFLAGS} ${LDFLAGS}

CC?=		cc

PIPE?=		-pipe

CFLAGS?=	-O2 ${PIPE} ${DEBUG}
COMPILE.c?=	${CC} ${CFLAGS} ${CPPFLAGS} -c
LINK.c?=	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS}

HOSTCC?=	cc

CXX?=		c++
CXXFLAGS?=	-O2 ${PIPE} ${DEBUG}
COMPILE.cc?=	${CXX} ${CXXFLAGS} ${CPPFLAGS} -c
LINK.cc?=	${CXX} ${CXXFLAGS} ${CPPFLAGS} ${LDFLAGS}

CPP?=		cpp
CPPFLAGS?=	

FC?=		f77
FFLAGS?=	-O2
RFLAGS?=
COMPILE.f?=	${FC} ${FFLAGS} -c
LINK.f?=	${FC} ${FFLAGS} ${LDFLAGS}
COMPILE.F?=	${FC} ${FFLAGS} ${CPPFLAGS} -c
LINK.F?=	${FC} ${FFLAGS} ${CPPFLAGS} ${LDFLAGS}
COMPILE.r?=	${FC} ${FFLAGS} ${RFLAGS} -c
LINK.r?=	${FC} ${FFLAGS} ${RFLAGS} ${LDFLAGS}

LEX?=		lex
LFLAGS?=
LEX.l?=		${LEX} ${LFLAGS}

LD?=		ld
LDFLAGS+=	${DEBUG}

MAKE?=		make

PC?=		pc
PFLAGS?=
COMPILE.p?=	${PC} ${PFLAGS} ${CPPFLAGS} -c
LINK.p?=	${PC} ${PFLAGS} ${CPPFLAGS} ${LDFLAGS}

SHELL?=		sh

YACC?=		yacc
YFLAGS?=	-d
YACC.y?=	${YACC} ${YFLAGS}

INSTALL?=	install

CTAGS?=		/usr/bin/ctags

# C
.c:
	${LINK.c} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.c.o:
	${COMPILE.c} ${.IMPSRC}
.c.a:
	${COMPILE.c} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# C++
.cc:
	${LINK.cc} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.cc.o:
	${COMPILE.cc} ${.IMPSRC}
.cc.a:
	${COMPILE.cc} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.C:
	${LINK.cc} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.C.o:
	${COMPILE.cc} ${.IMPSRC}
.C.a:
	${COMPILE.cc} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.cxx:
	${LINK.cc} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.cxx.o:
	${COMPILE.cc} ${.IMPSRC}
.cxx.a:
	${COMPILE.cc} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.cpp:
	${LINK.cc} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.cpp.o:
	${COMPILE.cc} ${.IMPSRC}
.cpp.a:
	${COMPILE.cc} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Fortran/Ratfor
.f:
	${LINK.f} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.f.o:
	${COMPILE.f} ${.IMPSRC}
.f.a:
	${COMPILE.f} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.F:
	${LINK.F} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.F.o:
	${COMPILE.F} ${.IMPSRC}
.F.a:
	${COMPILE.F} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

.r:
	${LINK.r} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.r.o:
	${COMPILE.r} ${.IMPSRC}
.r.a:
	${COMPILE.r} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Pascal
.p:
	${LINK.p} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.p.o:
	${COMPILE.p} ${.IMPSRC}
.p.a:
	${COMPILE.p} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Assembly
.s:
	${LINK.s} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.s.o:
	${COMPILE.s} ${.IMPSRC}
.s.a:
	${COMPILE.s} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o
.S:
	${LINK.S} -o ${.TARGET} ${.IMPSRC} ${LDLIBS}
.S.o:
	${COMPILE.S} ${.IMPSRC}
.S.a:
	${COMPILE.S} ${.IMPSRC}
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

# Lex
.l:
	${LEX.l} -o lex.${.PREFIX}.c ${.IMPSRC}
	${LINK.c} -o ${.TARGET} lex.${.PREFIX}.c ${LDLIBS} -ll
	rm -f lex.${.PREFIX}.c
.l.c:
	${LEX.l} -o ${.TARGET} ${.IMPSRC}
.l.o:
	${LEX.l} -o lex.${.PREFIX}.c ${.IMPSRC}
	${COMPILE.c} -o ${.TARGET} lex.${.PREFIX}.c
	rm -f lex.${.PREFIX}.c
	if test -f ${.TARGET:R}.d; then sed -i -e 's,lex.${.PREFIX}.c,${.IMPSRC},' ${.TARGET:R}.d; fi

# Yacc
.y:
	${YACC.y} ${.IMPSRC}
	${LINK.c} -o ${.TARGET} y.tab.c ${LDLIBS}
	rm -f y.tab.c
.y.c:
	${YACC.y} ${.IMPSRC}
	mv y.tab.c ${.TARGET}
.y.o:
	${YACC.y} ${.IMPSRC}
	${COMPILE.c} -o ${.TARGET} y.tab.c
	rm -f y.tab.c
	if test -f ${.TARGET:R}.d; then sed -i -e 's,y.tab.c,${.IMPSRC},' ${.TARGET:R}.d; fi

# Shell
.sh:
	rm -f ${.TARGET}
	cp ${.IMPSRC} ${.TARGET}
