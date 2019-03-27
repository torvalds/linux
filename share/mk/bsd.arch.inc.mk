#
# Include the arch-specific Makefile.inc.$ARCH.  We go from most specific
# to least specific, stopping after we get a hit.
#
.if exists(${.CURDIR}/Makefile.${MACHINE})
.include "Makefile.${MACHINE}"
.elif exists(${.CURDIR}/Makefile.${MACHINE_ARCH})
.include "Makefile.${MACHINE_ARCH}"
.elif exists(${.CURDIR}/Makefile.${MACHINE_CPUARCH})
.include "Makefile.${MACHINE_CPUARCH}"
.endif
