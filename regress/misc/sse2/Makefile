.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "amd64"
SUBDIR+= libbar foo

.elif make(regress) || make(all)
${.TARGETS}:
	@echo Cannot run on ${MACHINE_ARCH}
	@echo SKIPPED
.endif

.include <bsd.subdir.mk>
