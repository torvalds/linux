# $FreeBSD$

PROG_CXX=	llvm-objcopy
MAN=

SRCDIR=		tools/llvm-objcopy
SRCS+=		Buffer.cpp
SRCS+=		COFF/COFFObjcopy.cpp
SRCS+=		COFF/Object.cpp
SRCS+=		COFF/Reader.cpp
SRCS+=		COFF/Writer.cpp
SRCS+=		CopyConfig.cpp
SRCS+=		ELF/ELFObjcopy.cpp
SRCS+=		ELF/Object.cpp
SRCS+=		llvm-objcopy.cpp

.include "${SRCTOP}/lib/clang/llvm.pre.mk"

CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${LLVM_SRCS}/${SRCDIR}

.include "${SRCTOP}/lib/clang/llvm.build.mk"

.for hdr in ObjcopyOpts StripOpts
${hdr}.inc: ${LLVM_SRCS}/${SRCDIR}/${hdr}.td
	${LLVM_TBLGEN} -gen-opt-parser-defs \
	    -I ${LLVM_SRCS}/include -d ${.TARGET}.d -o ${.TARGET} \
	    ${LLVM_SRCS}/${SRCDIR}/${hdr}.td
TGHDRS+=	${hdr}.inc
.endfor

DEPENDFILES+=	${TGHDRS:C/$/.d/}
DPSRCS+=	${TGHDRS}
CLEANFILES+=	${TGHDRS} ${TGHDRS:C/$/.d/}

LIBDEPS+=	llvm

.for lib in ${LIBDEPS}
DPADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
LDADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
.endfor

PACKAGE=	clang

LIBADD+=	ncursesw
LIBADD+=	pthread
LIBADD+=	z

.include <bsd.prog.mk>
