# $FreeBSD$

.include <src.opts.mk>

SUBDIR+=	clang
SUBDIR+=	clang-tblgen
SUBDIR+=	llvm-tblgen

.if !defined(TOOLS_PREFIX)
SUBDIR+=	llvm-objdump

.if ${MK_CLANG_EXTRAS} != "no"
SUBDIR+=	bugpoint
SUBDIR+=	clang-format
SUBDIR+=	llc
SUBDIR+=	lli
SUBDIR+=	llvm-ar
SUBDIR+=	llvm-as
SUBDIR+=	llvm-bcanalyzer
SUBDIR+=	llvm-cxxdump
SUBDIR+=	llvm-cxxfilt
SUBDIR+=	llvm-diff
SUBDIR+=	llvm-dis
SUBDIR+=	llvm-dwarfdump
SUBDIR+=	llvm-extract
SUBDIR+=	llvm-link
SUBDIR+=	llvm-lto
SUBDIR+=	llvm-lto2
SUBDIR+=	llvm-mc
SUBDIR+=	llvm-mca
SUBDIR+=	llvm-modextract
SUBDIR+=	llvm-nm
SUBDIR+=	llvm-objcopy
SUBDIR+=	llvm-pdbutil
SUBDIR+=	llvm-rtdyld
SUBDIR+=	llvm-symbolizer
SUBDIR+=	llvm-xray
SUBDIR+=	opt
.endif

.if ${MK_LLD} != "no"
SUBDIR+=	lld
.endif
.if ${MK_LLDB} != "no"
SUBDIR+=	lldb
.endif
.if ${MK_LLVM_COV} != "no"
SUBDIR+=	llvm-cov
SUBDIR+=	llvm-profdata
.endif
.endif # TOOLS_PREFIX

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
