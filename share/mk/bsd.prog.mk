#	$OpenBSD: bsd.prog.mk,v 1.83 2021/06/09 19:44:55 mortimer Exp $
#	$NetBSD: bsd.prog.mk,v 1.55 1996/04/08 21:19:26 jtc Exp $
#	@(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91

.if exists(${.CURDIR}/../Makefile.inc)
.include "${.CURDIR}/../Makefile.inc"
.endif

.include <bsd.own.mk>

.SUFFIXES: .out .o .c .cc .cpp .C .cxx .y .l .s

.if (defined(LDSTATIC) && !defined(STATICPIE)) || defined(NOPIE)
CFLAGS+=       ${NOPIE_FLAGS}
CXXFLAGS+=     ${NOPIE_FLAGS}
AFLAGS+=       ${NOPIE_FLAGS}
LDFLAGS+=      ${NOPIE_LDFLAGS}
.endif

.if ${WARNINGS:L} == "yes"
CFLAGS+=       ${CDIAGFLAGS}
CXXFLAGS+=     ${CXXDIAGFLAGS}
.endif
CFLAGS+=	${COPTS}
CXXFLAGS+=     ${CXXOPTS}

CRTBEGIN?=       ${DESTDIR}/usr/lib/crtbegin.o
CRTEND?=         ${DESTDIR}/usr/lib/crtend.o

LIBCRT0?=	${DESTDIR}/usr/lib/crt0.o
LIBC?=		${DESTDIR}/usr/lib/libc.a
LIBCBOR?=	${DESTDIR}/usr/lib/libcbor.a
LIBCRYPTO?=	${DESTDIR}/usr/lib/libcrypto.a
LIBCURSES?=	${DESTDIR}/usr/lib/libcurses.a
LIBEDIT?=	${DESTDIR}/usr/lib/libedit.a
LIBELF?=	${DESTDIR}/usr/lib/libelf.a
LIBEVENT?=	${DESTDIR}/usr/lib/libevent.a
LIBEXECINFO?=	${DESTDIR}/usr/lib/libexecinfo.a
LIBEXPAT?=	${DESTDIR}/usr/lib/libexpat.a
LIBFIDO2?=	${DESTDIR}/usr/lib/libfido2.a
LIBFORM?=	${DESTDIR}/usr/lib/libform.a
LIBFORMW?=	${DESTDIR}/usr/lib/libformw.a
LIBKEYNOTE?=	${DESTDIR}/usr/lib/libkeynote.a
LIBKVM?=	${DESTDIR}/usr/lib/libkvm.a
LIBL?=		${DESTDIR}/usr/lib/libl.a
LIBM?=		${DESTDIR}/usr/lib/libm.a
LIBMENU?=	${DESTDIR}/usr/lib/libmenu.a
LIBMENUW?=	${DESTDIR}/usr/lib/libmenuw.a
LIBRADIUS?=	${DESTDIR}/usr/lib/libradius.a
LIBOSSAUDIO?=	${DESTDIR}/usr/lib/libossaudio.a
LIBPANEL?=	${DESTDIR}/usr/lib/libpanel.a
LIBPANELW?=	${DESTDIR}/usr/lib/libpanelw.a
LIBPCAP?=	${DESTDIR}/usr/lib/libpcap.a
LIBPERL?=	${DESTDIR}/usr/lib/libperl.a
LIBPTHREAD?=	${DESTDIR}/usr/lib/libpthread.a
LIBRPCSVC?=	${DESTDIR}/usr/lib/librpcsvc.a
LIBSKEY?=	${DESTDIR}/usr/lib/libskey.a
LIBSNDIO?=	${DESTDIR}/usr/lib/libsndio.a
LIBSSL?=	${DESTDIR}/usr/lib/libssl.a
LIBAGENTX?=	${DESTDIR}/usr/lib/libagentx.a
LIBTLS?=	${DESTDIR}/usr/lib/libtls.a
LIBTERMCAP?=	${DESTDIR}/usr/lib/libtermcap.a
LIBTERMLIB?=	${DESTDIR}/usr/lib/libtermlib.a
LIBUSBHID?=	${DESTDIR}/usr/lib/libusbhid.a
LIBUTIL?=	${DESTDIR}/usr/lib/libutil.a
LIBY?=		${DESTDIR}/usr/lib/liby.a
LIBZ?=		${DESTDIR}/usr/lib/libz.a

.if ${MACHINE_ARCH} == "alpha" || ${MACHINE_ARCH} == "amd64" || \
    ${MACHINE_ARCH} == "arm" || ${MACHINE_ARCH} == "i386"
LIBARCH?=	${DESTDIR}/usr/lib/lib${MACHINE_ARCH}.a
.else
LIBARCH?=
.endif

.if defined(PROG)
SRCS ?=	${PROG}.c
MAN ?= ${PROG}.1
.endif

# if we have several progs, define appropriate vars instead
.if defined(PROGS)
.  for p in ${PROGS}
SRCS_$p ?= $p.c
OBJS_$p = ${SRCS_$p:N*.h:N*.sh:R:S/$/.o/}
DPADD_$p ?= ${DPADD}
LDADD_$p ?= ${LDADD}

# XXX still create SRCS, because it's what bsd.dep.mk understands
SRCS += ${SRCS_$p}

# and we can write the actual target already
.    if defined(OBJS_$p) && !empty(OBJS_$p)
.      if !empty(SRCS_$p:M*.C) || !empty(SRCS_$p:M*.cc) || \
	!empty(SRCS_$p:M*.cpp) || !empty(SRCS_$p:M*.cxx)
$p: ${LIBCRT0} ${OBJS_$p} ${LIBC} ${CRTBEGIN} ${CRTEND} ${DPADD_$p}
	${CXX} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${OBJS_$p} ${LDADD_$p}
.      else
$p: ${LIBCRT0} ${OBJS_$p} ${LIBC} ${CRTBEGIN} ${CRTEND} ${DPADD_$p}
	${CC} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${OBJS_$p} ${LDADD_$p}
.      endif
.    endif
.  endfor
MAN ?= ${PROGS:=.1}
.endif

.if defined(PROG) || defined(PROGS)
# ... so we create appropriate full list of obj, dep, lex, yacc...
.  if !empty(SRCS:N*.h:N*.sh)
OBJS+=	${SRCS:N*.h:N*.sh:R:S/$/.o/}
DEPS+=	${OBJS:.o=.d}
.  endif

_LEXINTM?=${SRCS:M*.l:.l=.c}
_YACCINTM?=${SRCS:M*.y:.y=.c}
.endif

.if defined(PROG)
.  if defined(OBJS) && !empty(OBJS)
.    if !empty(SRCS:M*.C) || !empty(SRCS:M*.cc) || \
	!empty(SRCS:M*.cpp) || !empty(SRCS:M*.cxx)
${PROG}: ${LIBCRT0} ${OBJS} ${LIBC} ${CRTBEGIN} ${CRTEND} ${DPADD}
	${CXX} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${OBJS} ${LDADD}
.    else
${PROG}: ${LIBCRT0} ${OBJS} ${LIBC} ${CRTBEGIN} ${CRTEND} ${DPADD}
	${CC} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${OBJS} ${LDADD}
.    endif
.  endif	# defined(OBJS) && !empty(OBJS)
.endif

.MAIN: all
all: ${PROG} ${PROGS} _SUBDIRUSE

BUILDAFTER += ${PROG} ${PROGS} ${OBJS}

.if !target(clean)
clean: _SUBDIRUSE
	rm -f a.out [Ee]rrs mklog *.core y.tab.h \
	    ${PROG} ${PROGS} ${OBJS} ${_LEXINTM} ${_YACCINTM} ${CLEANFILES}
.endif

cleandir: _SUBDIRUSE clean

.if !target(install)
.if !target(beforeinstall)
beforeinstall:
.endif
.if !target(afterinstall)
afterinstall:
.endif

.if !target(realinstall)
realinstall:
.  if defined(PROG)
	${INSTALL} ${INSTALL_COPY} ${INSTALL_STRIP} \
	    -o ${BINOWN} -g ${BINGRP} \
	    -m ${BINMODE} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
.  endif
.  if defined(PROGS)
.    for p in ${PROGS}
	${INSTALL} ${INSTALL_COPY} ${INSTALL_STRIP} \
	    -o ${BINOWN} -g ${BINGRP} \
	    -m ${BINMODE} $p ${DESTDIR}${BINDIR}/$p
.    endfor
.  endif
.endif

install: maninstall _SUBDIRUSE
.if defined(LINKS) && !empty(LINKS)
.  for lnk file in ${LINKS}
	@l=${DESTDIR}${lnk}; \
	 t=${DESTDIR}${file}; \
	 echo $$t -\> $$l; \
	 rm -f $$t; ln $$l $$t
.  endfor
.endif

maninstall: afterinstall
afterinstall: realinstall
realinstall: beforeinstall
.endif

.if !defined(NOMAN)
.include <bsd.man.mk>
.endif

# if we already got bsd.lib.mk we don't want to wreck that
.if !defined(_LIBS)
.s.o:
	${COMPILE.S} -MD -MF ${.TARGET:R}.d -o $@ ${.IMPSRC}

.S.o:
	${COMPILE.S} -MD -MF ${.TARGET:R}.d -o $@ ${.IMPSRC}
.endif

.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.subdir.mk>
.include <bsd.sys.mk>
