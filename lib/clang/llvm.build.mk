# $FreeBSD$

.include <src.opts.mk>

.ifndef LLVM_SRCS
.error Please define LLVM_SRCS before including this file
.endif

.ifndef SRCDIR
.error Please define SRCDIR before including this file
.endif

.PATH:		${LLVM_SRCS}/${SRCDIR}

CFLAGS+=	-I${SRCTOP}/lib/clang/include
CFLAGS+=	-I${LLVM_SRCS}/include
CFLAGS+=	-DLLVM_BUILD_GLOBAL_ISEL
CFLAGS+=	-D__STDC_LIMIT_MACROS
CFLAGS+=	-D__STDC_CONSTANT_MACROS
#CFLAGS+=	-DNDEBUG

TARGET_ARCH?=	${MACHINE_ARCH}
BUILD_ARCH?=	${MACHINE_ARCH}

# Armv6 and armv7 uses hard float abi, unless the CPUTYPE has soft in it.
# arm (for armv4 and armv5 CPUs) always uses the soft float ABI.
# For all other targets, we stick with 'unknown'.
.if ${TARGET_ARCH:Marmv[67]*} && (!defined(CPUTYPE) || ${CPUTYPE:M*soft*} == "")
TARGET_ABI=	-gnueabihf
.elif ${TARGET_ARCH:Marm*}
TARGET_ABI=	-gnueabi
.else
TARGET_ABI=
.endif
VENDOR=		unknown
OS_VERSION=	freebsd13.0

LLVM_TARGET_TRIPLE?=	${TARGET_ARCH:C/amd64/x86_64/:C/arm64/aarch64/}-${VENDOR}-${OS_VERSION}${TARGET_ABI}
LLVM_BUILD_TRIPLE?=	${BUILD_ARCH:C/amd64/x86_64/:C/arm64/aarch64/}-${VENDOR}-${OS_VERSION}

CFLAGS+=	-DLLVM_DEFAULT_TARGET_TRIPLE=\"${LLVM_TARGET_TRIPLE}\"
CFLAGS+=	-DLLVM_HOST_TRIPLE=\"${LLVM_BUILD_TRIPLE}\"
CFLAGS+=	-DDEFAULT_SYSROOT=\"${TOOLS_PREFIX}\"

.if ${MK_LLVM_TARGET_AARCH64} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_AARCH64
. if ${MACHINE_CPUARCH} == "aarch64"
LLVM_NATIVE_ARCH=	AArch64
. endif
.endif
.if ${MK_LLVM_TARGET_ARM} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_ARM
. if ${MACHINE_CPUARCH} == "arm"
LLVM_NATIVE_ARCH=	ARM
. endif
.endif
.if ${MK_LLVM_TARGET_BPF} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_BPF
.endif
.if ${MK_LLVM_TARGET_MIPS} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_MIPS
. if ${MACHINE_CPUARCH} == "mips"
LLVM_NATIVE_ARCH=	Mips
. endif
.endif
.if ${MK_LLVM_TARGET_POWERPC} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_POWERPC
. if ${MACHINE_CPUARCH} == "powerpc"
LLVM_NATIVE_ARCH=	PowerPC
. endif
.endif
.if ${MK_LLVM_TARGET_SPARC} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_SPARC
. if ${MACHINE_CPUARCH} == "sparc64"
LLVM_NATIVE_ARCH=	Sparc
. endif
.endif
.if ${MK_LLVM_TARGET_X86} != "no"
CFLAGS+=	-DLLVM_TARGET_ENABLE_X86
. if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
LLVM_NATIVE_ARCH=	X86
. endif
.endif

.ifdef LLVM_NATIVE_ARCH
CFLAGS+=	-DLLVM_NATIVE_ASMPARSER=LLVMInitialize${LLVM_NATIVE_ARCH}AsmParser
CFLAGS+=	-DLLVM_NATIVE_ASMPRINTER=LLVMInitialize${LLVM_NATIVE_ARCH}AsmPrinter
CFLAGS+=	-DLLVM_NATIVE_DISASSEMBLER=LLVMInitialize${LLVM_NATIVE_ARCH}Disassembler
CFLAGS+=	-DLLVM_NATIVE_TARGET=LLVMInitialize${LLVM_NATIVE_ARCH}Target
CFLAGS+=	-DLLVM_NATIVE_TARGETINFO=LLVMInitialize${LLVM_NATIVE_ARCH}TargetInfo
CFLAGS+=	-DLLVM_NATIVE_TARGETMC=LLVMInitialize${LLVM_NATIVE_ARCH}TargetMC
.endif

CFLAGS+=	-ffunction-sections
CFLAGS+=	-fdata-sections
LDFLAGS+=	-Wl,--gc-sections

CXXFLAGS+=	-std=c++11
CXXFLAGS+=	-fno-exceptions
CXXFLAGS+=	-fno-rtti
CXXFLAGS.clang+= -stdlib=libc++

.if ${MACHINE_CPUARCH} == "arm"
STATIC_CFLAGS+= -mlong-calls
STATIC_CXXFLAGS+= -mlong-calls
.endif
