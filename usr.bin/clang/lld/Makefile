# $FreeBSD$

.include <src.opts.mk>

LLVM_SRCS=	${SRCTOP}/contrib/llvm
LLD_SRCS=	${LLVM_SRCS}/tools/lld

PACKAGE=	lld
PROG_CXX=	ld.lld
# Man page directory
.PATH:		${LLD_SRCS}/docs
.if (!defined(TOOLS_PREFIX) && ${MK_LLD_IS_LD} != "no") || \
    (defined(TOOLS_PREFIX) && ${MK_LLD_BOOTSTRAP} != "no")
SYMLINKS=	${PROG_CXX} ${BINDIR}/ld
MLINKS=		ld.lld.1 ld.1
.endif

CFLAGS+=	-I${LLD_SRCS}/ELF
CFLAGS+=	-I${LLD_SRCS}/include
CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${OBJTOP}/lib/clang/libllvm

SRCDIR=		tools/lld
SRCS+=		Common/Args.cpp
SRCS+=		Common/ErrorHandler.cpp
SRCS+=		Common/Memory.cpp
SRCS+=		Common/Reproduce.cpp
SRCS+=		Common/Strings.cpp
SRCS+=		Common/TargetOptionsCommandFlags.cpp
SRCS+=		Common/Threads.cpp
SRCS+=		Common/Version.cpp
SRCS+=		ELF/AArch64ErrataFix.cpp
SRCS+=		ELF/Arch/AArch64.cpp
SRCS+=		ELF/Arch/AMDGPU.cpp
SRCS+=		ELF/Arch/ARM.cpp
SRCS+=		ELF/Arch/AVR.cpp
SRCS+=		ELF/Arch/Hexagon.cpp
SRCS+=		ELF/Arch/MSP430.cpp
SRCS+=		ELF/Arch/Mips.cpp
SRCS+=		ELF/Arch/MipsArchTree.cpp
SRCS+=		ELF/Arch/PPC.cpp
SRCS+=		ELF/Arch/PPC64.cpp
SRCS+=		ELF/Arch/RISCV.cpp
SRCS+=		ELF/Arch/SPARCV9.cpp
SRCS+=		ELF/Arch/X86.cpp
SRCS+=		ELF/Arch/X86_64.cpp
SRCS+=		ELF/CallGraphSort.cpp
SRCS+=		ELF/DWARF.cpp
SRCS+=		ELF/Driver.cpp
SRCS+=		ELF/DriverUtils.cpp
SRCS+=		ELF/EhFrame.cpp
SRCS+=		ELF/Filesystem.cpp
SRCS+=		ELF/ICF.cpp
SRCS+=		ELF/InputFiles.cpp
SRCS+=		ELF/InputSection.cpp
SRCS+=		ELF/LTO.cpp
SRCS+=		ELF/LinkerScript.cpp
SRCS+=		ELF/MapFile.cpp
SRCS+=		ELF/MarkLive.cpp
SRCS+=		ELF/OutputSections.cpp
SRCS+=		ELF/Relocations.cpp
SRCS+=		ELF/ScriptLexer.cpp
SRCS+=		ELF/ScriptParser.cpp
SRCS+=		ELF/SymbolTable.cpp
SRCS+=		ELF/Symbols.cpp
SRCS+=		ELF/SyntheticSections.cpp
SRCS+=		ELF/Target.cpp
SRCS+=		ELF/Thunks.cpp
SRCS+=		ELF/Writer.cpp
SRCS+=		lib/Core/Error.cpp
SRCS+=		lib/Core/File.cpp
SRCS+=		lib/Core/LinkingContext.cpp
SRCS+=		lib/Core/Reader.cpp
SRCS+=		lib/Core/Resolver.cpp
SRCS+=		lib/Core/SymbolTable.cpp
SRCS+=		tools/lld/lld.cpp

.include "${SRCTOP}/lib/clang/llvm.build.mk"

LIBDEPS+=	llvm

.for lib in ${LIBDEPS}
DPADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
LDADD+=		${OBJTOP}/lib/clang/lib${lib}/lib${lib}.a
.endfor

LLVM_TBLGEN?=	llvm-tblgen
INCFILE=	Options.inc
TDFILE=		${LLD_SRCS}/ELF/Options.td
GENOPT=		-gen-opt-parser-defs
${INCFILE}: ${TDFILE}
	${LLVM_TBLGEN} ${GENOPT} -I ${LLVM_SRCS}/include -d ${.TARGET:C/$/.d/} \
	    -o ${.TARGET} ${TDFILE}
TGHDRS+=	${INCFILE}

DPSRCS+=	${TGHDRS}
CLEANFILES+=	${TGHDRS} ${TGHDRS:C/$/.d/}

LIBADD+=	ncursesw
LIBADD+=	pthread
LIBADD+=	z

.include <bsd.prog.mk>
