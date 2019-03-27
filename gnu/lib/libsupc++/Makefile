# $FreeBSD$

.include <src.opts.mk>

GCCVER=	4.2
GCCDIR=	${.CURDIR}/../../../contrib/gcc
GCCLIB=	${.CURDIR}/../../../contrib/gcclibs
SRCDIR=	${.CURDIR}/../../../contrib/libstdc++/libsupc++

.PATH: ${SRCDIR} ${GCCLIB}/libiberty

LIB=	supc++
SHLIB_MAJOR=1
SRCS+=	del_op.cc del_opnt.cc del_opv.cc del_opvnt.cc eh_alloc.cc eh_arm.cc \
	eh_aux_runtime.cc eh_call.cc eh_catch.cc eh_exception.cc eh_globals.cc \
	eh_personality.cc eh_term_handler.cc eh_terminate.cc eh_throw.cc \
	eh_type.cc eh_unex_handler.cc guard.cc new_handler.cc new_op.cc \
	new_opnt.cc new_opv.cc new_opvnt.cc pure.cc tinfo.cc tinfo2.cc \
	vec.cc vterminate.cc

# from libiberty:
SRCS+=	cp-demangle.c

CFLAGS+=	-DIN_GLIBCPP_V3 -DHAVE_CONFIG_H
CFLAGS+=	-I${GCCLIB}/include -I${SRCDIR} -I${GCCDIR}
CFLAGS+=	-I${.CURDIR}/../libstdc++ -I.
CFLAGS+=	-frandom-seed=RepeatabilityConsideredGood
CXXFLAGS+=	-fno-implicit-templates -ffunction-sections -fdata-sections
PO_CXXFLAGS=    ${CXXFLAGS:N-ffunction-sections}

HDRS=	exception new typeinfo cxxabi.h exception_defines.h
INCS=	${HDRS:S;^;${SRCDIR}/;}
INCSDIR=${INCLUDEDIR}/c++/${GCCVER}

.if ${MACHINE_CPUARCH} == "arm"
unwind.h: ${GCCDIR}/config/arm/unwind-arm.h
.else
unwind.h: ${GCCDIR}/unwind-generic.h
.endif

unwind.h: .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

SRCS+=		unwind.h
CLEANFILES+=	unwind.h

# Symbol versioning

VERSION_MAP=	${.CURDIR}/Version.map

CXXFLAGS.clang+= -stdlib=libstdc++

.include <bsd.lib.mk>

# Filter out libc++-specific flags, and -std= flags above c++98 or gnu++98.
CXXFLAGS:=	${CXXFLAGS:N-stdlib=libc++:N-std=c++[01][13x]:N-std=gnu++[01][13x]}
