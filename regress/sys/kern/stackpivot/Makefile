
.if ${MACHINE} != "arm64" && \
    ${MACHINE} != "amd64" && \
    ${MACHINE} != "i386" && \
    ${MACHINE} != "powerpc64" && \
    ${MACHINE} != "riscv64" && \
    ${MACHINE_ARCH} != "powerpc" && \
    ${MACHINE_ARCH} != "mips64" && \
    ${MACHINE_ARCH} != "mips64el"
REGRESS_TARGETS=run-regress-skiparch
run-regress-skiparch:
	# Need stack pivot asm for MACHINE ${MACHINE} / MACHINE_ARCH ${MACHINE_ARCH}
	@echo SKIPPED
.include <bsd.regress.mk>
.else
SUBDIR += syscall
SUBDIR += pagefault
.include <bsd.subdir.mk>
.endif


