# $FreeBSD$

.include "${SRCTOP}/lib/clang/lldb.pre.mk"

PACKAGE=	lldb
PROG_CXX=	lldb
# Man page directory
.PATH:		${LLDB_SRCS}/docs

CFLAGS+=	-I${LLDB_SRCS}/include
CFLAGS+=	-I${.OBJDIR}

SRCDIR=		tools/lldb/tools/driver
SRCS+=		Driver.cpp

.include "${SRCTOP}/lib/clang/clang.build.mk"

LIBDEPS+=	lldb
LIBDEPS+=	clang
LIBDEPS+=	llvm

.for lib in ${LIBDEPS}
DPADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
LDADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
.endfor

LLVM_TBLGEN?=	llvm-tblgen
INCFILE=	Options.inc
TDFILE=		${LLDB_SRCS}/tools/driver/Options.td
GENOPT=		-gen-opt-parser-defs
${INCFILE}: ${TDFILE}
	${LLVM_TBLGEN} ${GENOPT} -I ${LLVM_SRCS}/include -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${TDFILE}
TGHDRS+=	${INCFILE}

DPSRCS+=	${TGHDRS}
CLEANFILES+=	${TGHDRS} ${TGHDRS:C/$/.d/}

LIBADD+=	edit
LIBADD+=	panel
LIBADD+=	ncursesw
LIBADD+=	pthread
LIBADD+=	z

.include <bsd.prog.mk>
