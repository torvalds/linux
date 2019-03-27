# $FreeBSD$

.if ${.MAKE.MODE:Mmeta*} != ""
.if !empty(SUBDIR) && !defined(LIB) && !defined(PROG) && ${.MAKE.MAKEFILES:M*bsd.prog.mk} == ""
.if ${.MAKE.MODE:Mleaf*} != ""
# we only want leaf dirs to build in meta mode... and we are not one
.MAKE.MODE = normal
.endif
.endif
.endif

# XXX: This should be combined with external compiler support in Makefile.inc1
# and local.meta.sys.mk (CROSS_TARGET_FLAGS)
.if ${MK_SYSROOT} == "yes" && !empty(SYSROOT) && ${MACHINE} != "host"
CFLAGS_LAST+= --sysroot=${SYSROOT}
CXXFLAGS_LAST+= --sysroot=${SYSROOT}
LDADD+= --sysroot=${SYSROOT}
.elif ${MK_STAGING} == "yes"
CFLAGS+= -isystem ${STAGE_INCLUDEDIR}
# XXX: May be needed for GCC to build with libc++ rather than libstdc++. See Makefile.inc1
#CXXFLAGS+= -std=gnu++11
#LDADD+= -L${STAGE_LIBDIR}/libc++
#CXXFLAGS+= -I${STAGE_INCLUDEDIR}/usr/include/c++/v1
LDADD+= -L${STAGE_LIBDIR}
.endif

.if ${MACHINE} == "host"
.if ${.MAKE.DEPENDFILE:E} != "host"
UPDATE_DEPENDFILE?= no
.endif
HOST_CFLAGS+= -DHOSTPROG
CFLAGS+= ${HOST_CFLAGS}
.endif

.-include "src.init.mk"
