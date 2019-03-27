# $FreeBSD$

# building clang is particularly tedious - so avoid re-inventing wheels

all: bootstrap-toolchain

UPDATE_DEPENDFILE= no
.export UPDATE_DEPENDFILE

# we build for the pseudo machine "host"
TARGET_MACHINE= host

HOST_MACHINE!= uname -m
HOST_MACHINE_ARCH!= uname -p
BTOOLSDIR?= ${HOST_OBJTOP}/tools
WORLDTMP= ${BTOOLSDIR}
LEGACY_TOOLS?= ${BTOOLSDIR}/legacy

BSENV= \
	MAKESYSPATH=${SRCTOP}/tools/build/mk:${SRCTOP}/share/mk \
	TARGET=${HOST_MACHINE} TARGET_ARCH=${HOST_MACHINE_ARCH} \
	WITHOUT_STAGING=1 STAGE_ROOT= BOOTSTRAPPING_TOOLS=1 \
	WORLDTMP=${BTOOLSDIR} \
	INSTALL="sh ${SRCTOP}/tools/install.sh" \
	PATH=${LEGACY_TOOLS}/usr/sbin:${LEGACY_TOOLS}/usr/bin:${LEGACY_TOOLS}/bin:${PATH}

.if !defined(OSRELDATE)
ord_h= /usr/include/osreldate.h
.if exists(${ord_h})
OSRELDATE!= sed -n '/define.*__FreeBSD_version/{s,^[^0-9]*,,p;q;}' ${ord_h}
.endif
OSRELDATE?= 0
.endif

# need to keep this in sync with src/Makefile.inc1 
BSARGS= 	DESTDIR= \
		OBJTOP='${WORLDTMP}/obj-tools' \
		OBJROOT='$${OBJTOP}/' \
		MAKEOBJDIRPREFIX= \
		BOOTSTRAPPING=${OSRELDATE} \
		BWPHASE=${.TARGET} \
		SSP_CFLAGS= \
		MK_HTML=no NO_LINT=yes MK_MAN=no \
		-DNO_PIC MK_PROFILE=no -DNO_SHARED \
		-DNO_CPU_CFLAGS MK_WARNS=no MK_CTF=no \
		MK_CLANG_EXTRAS=no MK_CLANG_FULL=no \
		MK_LLDB=no MK_TESTS=no \
		MK_INCLUDES=yes

# We will handle building the toolchain and cross-compiler.
BSARGS+= MK_CROSS_COMPILER=no MK_CLANG=no MK_GCC=no

DISTRIB_ENV=	INSTALL="sh ${SRCTOP}/tools/install.sh" NO_FSCHG=1 MK_TESTS=no
legacy: .MAKE ${META_DEPS}
	mkdir -p ${LEGACY_TOOLS}
	${DISTRIB_ENV} ${MAKE} -C ${SRCTOP}/etc distrib-dirs \
	    DESTDIR=${BTOOLSDIR} > $@.distrib-dirs_btoolsdir
	${DISTRIB_ENV} ${MAKE} -C ${SRCTOP}/etc distrib-dirs \
	    DESTDIR=${LEGACY_TOOLS} > $@.distrib-dirs_legacy_tools
	${BSENV} ${MAKE} -C ${SRCTOP} -f Makefile.inc1 ${BSARGS} $@

bootstrap-tools: legacy
build-tools: bootstrap-tools
cross-tools: build-tools

cross-tools build-tools bootstrap-tools: .MAKE ${META_DEPS}
	${BSENV} ${MAKE} -C ${SRCTOP} -f Makefile.inc1 ${BSARGS} $@

# MAKELEVEL=0 so that dirdeps.mk does its thing
# BSENV:MPATH=* lets us use the bootstrapped stuff in LEGACY_TOOLS above.
# TARGET* is so that MK_CLANG gets set correctly.
BSTCENV= \
	MAKELEVEL=0 \
	MACHINE=host \
	BOOTSTRAPPING_TOOLS=1 \
	TARGET=${HOST_MACHINE} \
	TARGET_ARCH=${HOST_MACHINE_ARCH} \
	${BSENV:MPATH=*}

BSTCARGS= \
	${BSARGS:NDESTDIR=*:NOBJTOP=*:NOBJROOT=*:NMK_CROSS_COMPILER=*:NMK_CLANG=*:NMK_GCC=*} \
	BUILD_DIRDEPS=yes \
	LLVM_TBLGEN=${TOOLSDIR}/usr/bin/llvm-tblgen \
	CLANG_TBLGEN=${TOOLSDIR}/usr/bin/clang-tblgen \
	-DWITH_STAGING \
	-DWITH_TOOLSDIR

# finally we build toolchain leveraging the above.
bootstrap-toolchain:	.MAKE cross-tools
	${BSTCENV} ${MAKE} -C ${SRCTOP} ${BSTCARGS} toolchain

# Ensure CCACHE_DIR is ignored since we are processing .meta files here.
.include <bsd.compiler.mk>
