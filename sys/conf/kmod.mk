#	From: @(#)bsd.prog.mk	5.26 (Berkeley) 6/25/91
# $FreeBSD$
#
# The include file <bsd.kmod.mk> handles building and installing loadable
# kernel modules.
#
#
# +++ variables +++
#
# CLEANFILES	Additional files to remove for the clean and cleandir targets.
#
# EXPORT_SYMS	A list of symbols that should be exported from the module,
#		or the name of a file containing a list of symbols, or YES
#		to export all symbols.  If not defined, no symbols are
#		exported.
#
# KMOD		The name of the kernel module to build.
#
# KMODDIR	Base path for kernel modules (see kld(4)). [/boot/kernel]
#
# KMODOWN	Module file owner. [${BINOWN}]
#
# KMODGRP	Module file group. [${BINGRP}]
#
# KMODMODE	Module file mode. [${BINMODE}]
#
# KMODLOAD	Command to load a kernel module [/sbin/kldload]
#
# KMODUNLOAD	Command to unload a kernel module [/sbin/kldunload]
#
# KMODISLOADED	Command to check whether a kernel module is
#		loaded [/sbin/kldstat -q -n]
#
# PROG		The name of the kernel module to build.
#		If not supplied, ${KMOD}.ko is used.
#
# SRCS		List of source files.
#
# FIRMWS	List of firmware images in format filename:shortname:version
#
# FIRMWARE_LICENSE
#		Set to the name of the license the user has to agree on in
#		order to use this firmware. See /usr/share/doc/legal
#
# DESTDIR	The tree where the module gets installed. [not set]
#
# KERNBUILDDIR
#		Set to the location of the kernel build directory where
#		the opt_*.h files, .o's and kernel winds up.
#
# +++ targets +++
#
# 	install:
#               install the kernel module; if the Makefile
#               does not itself define the target install, the targets
#               beforeinstall and afterinstall may also be used to cause
#               actions immediately before and after the install target
#		is executed.
#
# 	load:
#		Load a module.
#
# 	unload:
#		Unload a module.
#
#	reload:
#		Unload if loaded, then load.
#

AWK?=		awk
KMODLOAD?=	/sbin/kldload
KMODUNLOAD?=	/sbin/kldunload
KMODISLOADED?=	/sbin/kldstat -q -n
OBJCOPY?=	objcopy

.include <bsd.init.mk>
# Grab all the options for a kernel build. For backwards compat, we need to
# do this after bsd.own.mk.
.include "kern.opts.mk"
.include <bsd.compiler.mk>
.include "config.mk"

# Search for kernel source tree in standard places.
.if empty(KERNBUILDDIR)
.if !defined(SYSDIR)
.for _dir in ${SRCTOP:D${SRCTOP}/sys} \
    ${.CURDIR}/../.. ${.CURDIR}/../../.. /sys /usr/src/sys
.if !defined(SYSDIR) && exists(${_dir}/kern/)
SYSDIR=	${_dir:tA}
.endif
.endfor
.endif
.if !defined(SYSDIR) || !exists(${SYSDIR}/kern/)
.error "can't find kernel source tree"
.endif
.endif

.SUFFIXES: .out .o .c .cc .cxx .C .y .l .s .S .m

# amd64 and mips use direct linking for kmod, all others use shared binaries
.if ${MACHINE_CPUARCH} != amd64 && ${MACHINE_CPUARCH} != mips
__KLD_SHARED=yes
.else
__KLD_SHARED=no
.endif

.if !empty(CFLAGS:M-O[23s]) && empty(CFLAGS:M-fno-strict-aliasing)
CFLAGS+=	-fno-strict-aliasing
.endif
WERROR?=	-Werror
CFLAGS+=	${WERROR}
CFLAGS+=	-D_KERNEL
CFLAGS+=	-DKLD_MODULE
.if defined(MODULE_TIED)
CFLAGS+=	-DKLD_TIED
.endif

# Don't use any standard or source-relative include directories.
NOSTDINC=	-nostdinc
CFLAGS:=	${CFLAGS:N-I*} ${NOSTDINC} ${INCLMAGIC} ${CFLAGS:M-I*}
.if defined(KERNBUILDDIR)
CFLAGS+=	-DHAVE_KERNEL_OPTION_HEADERS -include ${KERNBUILDDIR}/opt_global.h
.endif

# Add -I paths for system headers.  Individual module makefiles don't
# need any -I paths for this.  Similar defaults for .PATH can't be
# set because there are no standard paths for non-headers.
CFLAGS+=	-I. -I${SYSDIR} -I${SYSDIR}/contrib/ck/include

CFLAGS.gcc+=	-finline-limit=${INLINE_LIMIT}
CFLAGS.gcc+=	-fms-extensions
CFLAGS.gcc+= --param inline-unit-growth=100
CFLAGS.gcc+= --param large-function-growth=1000

# Disallow common variables, and if we end up with commons from
# somewhere unexpected, allocate storage for them in the module itself.
CFLAGS+=	-fno-common
LDFLAGS+=	-d -warn-common

.if defined(LINKER_FEATURES) && ${LINKER_FEATURES:Mbuild-id}
LDFLAGS+=	-Wl,--build-id=sha1
.endif

CFLAGS+=	${DEBUG_FLAGS}
.if ${MACHINE_CPUARCH} == amd64
CFLAGS+=	-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer
.endif

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "riscv"
CFLAGS+=	-fPIC
.endif

# Temporary workaround for PR 196407, which contains the fascinating details.
# Don't allow clang to use fpu instructions or registers in kernel modules.
.if ${MACHINE_CPUARCH} == arm
.if ${COMPILER_VERSION} < 30800
CFLAGS.clang+=	-mllvm -arm-use-movt=0
.else
CFLAGS.clang+=	-mno-movt
.endif
CFLAGS.clang+=	-mfpu=none
CFLAGS+=	-funwind-tables
.endif

.if ${MACHINE_CPUARCH} == powerpc
CFLAGS+=	-mlongcall -fno-omit-frame-pointer
.endif

.if ${MACHINE_CPUARCH} == mips
CFLAGS+=	-G0 -fno-pic -mno-abicalls -mlong-calls
.endif

.if defined(DEBUG) || defined(DEBUG_FLAGS)
CTFFLAGS+=	-g
.endif

.if defined(FIRMWS)
${KMOD:S/$/.c/}: ${SYSDIR}/tools/fw_stub.awk
	${AWK} -f ${SYSDIR}/tools/fw_stub.awk ${FIRMWS} -m${KMOD} -c${KMOD:S/$/.c/g} \
	    ${FIRMWARE_LICENSE:C/.+/-l/}${FIRMWARE_LICENSE}

SRCS+=	${KMOD:S/$/.c/}
CLEANFILES+=	${KMOD:S/$/.c/}

.for _firmw in ${FIRMWS}
${_firmw:C/\:.*$/.fwo/:T}:	${_firmw:C/\:.*$//}
	@${ECHO} ${_firmw:C/\:.*$//} ${.ALLSRC:M*${_firmw:C/\:.*$//}}
	@if [ -e ${_firmw:C/\:.*$//} ]; then			\
		${LD} -b binary --no-warn-mismatch ${_LDFLAGS}	\
		    -m ${LD_EMULATION} -r -d			\
		    -o ${.TARGET} ${_firmw:C/\:.*$//};		\
	else							\
		ln -s ${.ALLSRC:M*${_firmw:C/\:.*$//}} ${_firmw:C/\:.*$//}; \
		${LD} -b binary --no-warn-mismatch ${_LDFLAGS}	\
		    -m ${LD_EMULATION} -r -d			\
		    -o ${.TARGET} ${_firmw:C/\:.*$//};		\
		rm ${_firmw:C/\:.*$//};				\
	fi

OBJS+=	${_firmw:C/\:.*$/.fwo/:T}
.endfor
.endif

# Conditionally include SRCS based on kernel config options.
.for _o in ${KERN_OPTS}
SRCS+=${SRCS.${_o}}
.endfor

OBJS+=	${SRCS:N*.h:R:S/$/.o/g}

.if !defined(PROG)
PROG=	${KMOD}.ko
.endif

.if !defined(DEBUG_FLAGS)
FULLPROG=	${PROG}
.else
FULLPROG=	${PROG}.full
${PROG}: ${FULLPROG} ${PROG}.debug
	${OBJCOPY} --strip-debug --add-gnu-debuglink=${PROG}.debug \
	    ${FULLPROG} ${.TARGET}
${PROG}.debug: ${FULLPROG}
	${OBJCOPY} --only-keep-debug ${FULLPROG} ${.TARGET}
.endif

.if ${__KLD_SHARED} == yes
${FULLPROG}: ${KMOD}.kld
	${LD} -m ${LD_EMULATION} -Bshareable -znotext ${_LDFLAGS} \
	    -o ${.TARGET} ${KMOD}.kld
.if !defined(DEBUG_FLAGS)
	${OBJCOPY} --strip-debug ${.TARGET}
.endif
.endif

EXPORT_SYMS?=	NO
.if ${EXPORT_SYMS} != YES
CLEANFILES+=	export_syms
.endif

.if ${__KLD_SHARED} == yes
${KMOD}.kld: ${OBJS}
.else
${FULLPROG}: ${OBJS}
.endif
	${LD} -m ${LD_EMULATION} ${_LDFLAGS} -r -d -o ${.TARGET} ${OBJS}
.if ${MK_CTF} != "no"
	${CTFMERGE} ${CTFFLAGS} -o ${.TARGET} ${OBJS}
.endif
.if defined(EXPORT_SYMS)
.if ${EXPORT_SYMS} != YES
.if ${EXPORT_SYMS} == NO
	:> export_syms
.elif !exists(${.CURDIR}/${EXPORT_SYMS})
	echo -n "${EXPORT_SYMS:@s@$s${.newline}@}" > export_syms
.else
	grep -v '^#' < ${EXPORT_SYMS} > export_syms
.endif
	${AWK} -f ${SYSDIR}/conf/kmod_syms.awk ${.TARGET} \
	    export_syms | xargs -J% ${OBJCOPY} % ${.TARGET}
.endif
.endif # defined(EXPORT_SYMS)
.if defined(PREFIX_SYMS)
	${AWK} -v prefix=${PREFIX_SYMS} -f ${SYSDIR}/conf/kmod_syms_prefix.awk \
	    ${.TARGET} /dev/null | xargs -J% ${OBJCOPY} % ${.TARGET}
.endif
.if !defined(DEBUG_FLAGS) && ${__KLD_SHARED} == no
	${OBJCOPY} --strip-debug ${.TARGET}
.endif

.if ${COMPILER_TYPE} == "clang" || \
    (${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} >= 60000)
_MAP_DEBUG_PREFIX= yes
.endif

_ILINKS=machine
.if ${MACHINE} != ${MACHINE_CPUARCH} && ${MACHINE} != "arm64"
_ILINKS+=${MACHINE_CPUARCH}
.endif
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
_ILINKS+=x86
.endif
CLEANFILES+=${_ILINKS}

all: ${PROG}

beforedepend: ${_ILINKS}
beforebuild: ${_ILINKS}

# Ensure that the links exist without depending on it when it exists which
# causes all the modules to be rebuilt when the directory pointed to changes.
# Ensure that debug info references the path in the source tree.
.for _link in ${_ILINKS}
.if !exists(${.OBJDIR}/${_link})
OBJS_DEPEND_GUESS+=	${_link}
.endif
.if defined(_MAP_DEBUG_PREFIX)
.if ${_link} == "machine"
CFLAGS+= -fdebug-prefix-map=./machine=${SYSDIR}/${MACHINE}/include
.else
CFLAGS+= -fdebug-prefix-map=./${_link}=${SYSDIR}/${_link}/include
.endif
.endif
.endfor

.if defined(_MAP_DEBUG_PREFIX)
# Ensure that DWARF info contains a full path for auto-generated headers.
CFLAGS+= -fdebug-prefix-map=.=${.OBJDIR}
.endif

.NOPATH: ${_ILINKS}

${_ILINKS}:
	@case ${.TARGET} in \
	machine) \
		path=${SYSDIR}/${MACHINE}/include ;; \
	*) \
		path=${SYSDIR}/${.TARGET:T}/include ;; \
	esac ; \
	path=`(cd $$path && /bin/pwd)` ; \
	${ECHO} ${.TARGET:T} "->" $$path ; \
	ln -fns $$path ${.TARGET:T}

CLEANFILES+= ${PROG} ${KMOD}.kld ${OBJS}

.if defined(DEBUG_FLAGS)
CLEANFILES+= ${FULLPROG} ${PROG}.debug
.endif

.if !target(install)

_INSTALLFLAGS:=	${INSTALLFLAGS}
.for ie in ${INSTALLFLAGS_EDIT}
_INSTALLFLAGS:=	${_INSTALLFLAGS${ie}}
.endfor

.if !target(realinstall)
KERN_DEBUGDIR?=	${DEBUGDIR}
realinstall: _kmodinstall
.ORDER: beforeinstall _kmodinstall
_kmodinstall: .PHONY
	${INSTALL} -T release -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG} ${DESTDIR}${KMODDIR}/
.if defined(DEBUG_FLAGS) && !defined(INSTALL_NODEBUG) && ${MK_KERNEL_SYMBOLS} != "no"
	${INSTALL} -T debug -o ${KMODOWN} -g ${KMODGRP} -m ${KMODMODE} \
	    ${_INSTALLFLAGS} ${PROG}.debug ${DESTDIR}${KERN_DEBUGDIR}${KMODDIR}/
.endif

.include <bsd.links.mk>

.if !defined(NO_XREF)
afterinstall: _kldxref
.ORDER: realinstall _kldxref
.ORDER: _installlinks _kldxref
_kldxref: .PHONY
	@if type kldxref >/dev/null 2>&1; then \
		${ECHO} kldxref ${DESTDIR}${KMODDIR}; \
		kldxref ${DESTDIR}${KMODDIR}; \
	fi
.endif
.endif # !target(realinstall)

.endif # !target(install)

.if !target(load)
load: ${PROG} .PHONY
	${KMODLOAD} -v ${.OBJDIR}/${PROG}
.endif

.if !target(unload)
unload: .PHONY
	if ${KMODISLOADED} ${PROG} ; then ${KMODUNLOAD} -v ${PROG} ; fi
.endif

.if !target(reload)
reload: unload load .PHONY
.endif

.if defined(KERNBUILDDIR)
.PATH: ${KERNBUILDDIR}
CFLAGS+=	-I${KERNBUILDDIR}
.for _src in ${SRCS:Mopt_*.h}
CLEANFILES+=	${_src}
.if !target(${_src})
${_src}:
	ln -sf ${KERNBUILDDIR}/${_src} ${.TARGET}
.endif
.endfor
.else
.for _src in ${SRCS:Mopt_*.h}
CLEANFILES+=	${_src}
.if !target(${_src})
${_src}:
	:> ${.TARGET}
.endif
.endfor
.endif

# Add the sanitizer C flags
CFLAGS+=	${SAN_CFLAGS}

# Add the gcov flags
CFLAGS+=	${GCOV_CFLAGS}

# Respect configuration-specific C flags.
CFLAGS+=	${ARCH_FLAGS} ${CONF_CFLAGS}

.if !empty(SRCS:Mvnode_if.c)
CLEANFILES+=	vnode_if.c
vnode_if.c: ${SYSDIR}/tools/vnode_if.awk ${SYSDIR}/kern/vnode_if.src
	${AWK} -f ${SYSDIR}/tools/vnode_if.awk ${SYSDIR}/kern/vnode_if.src -c
.endif

.if !empty(SRCS:Mvnode_if.h)
CLEANFILES+=	vnode_if.h vnode_if_newproto.h vnode_if_typedef.h
vnode_if.h vnode_if_newproto.h vnode_if_typedef.h: ${SYSDIR}/tools/vnode_if.awk \
    ${SYSDIR}/kern/vnode_if.src
vnode_if.h: vnode_if_newproto.h vnode_if_typedef.h
	${AWK} -f ${SYSDIR}/tools/vnode_if.awk ${SYSDIR}/kern/vnode_if.src -h
vnode_if_newproto.h:
	${AWK} -f ${SYSDIR}/tools/vnode_if.awk ${SYSDIR}/kern/vnode_if.src -p
vnode_if_typedef.h:
	${AWK} -f ${SYSDIR}/tools/vnode_if.awk ${SYSDIR}/kern/vnode_if.src -q
.endif

# Build _if.[ch] from _if.m, and clean them when we're done.
# __MPATH defined in config.mk
_MFILES=${__MPATH:T:O}
_MPATH=${__MPATH:H:O:u}
.PATH.m: ${_MPATH}
.for _i in ${SRCS:M*_if.[ch]}
_MATCH=M${_i:R:S/$/.m/}
_MATCHES=${_MFILES:${_MATCH}}
.if !empty(_MATCHES)
CLEANFILES+=	${_i}
.endif
.endfor # _i
.m.c:	${SYSDIR}/tools/makeobjops.awk
	${AWK} -f ${SYSDIR}/tools/makeobjops.awk ${.IMPSRC} -c

.m.h:	${SYSDIR}/tools/makeobjops.awk
	${AWK} -f ${SYSDIR}/tools/makeobjops.awk ${.IMPSRC} -h

.for _i in mii pccard
.if !empty(SRCS:M${_i}devs.h)
CLEANFILES+=	${_i}devs.h
${_i}devs.h: ${SYSDIR}/tools/${_i}devs2h.awk ${SYSDIR}/dev/${_i}/${_i}devs
	${AWK} -f ${SYSDIR}/tools/${_i}devs2h.awk ${SYSDIR}/dev/${_i}/${_i}devs
.endif
.endfor # _i

.if !empty(SRCS:Mbhnd_nvram_map.h)
CLEANFILES+=	bhnd_nvram_map.h
bhnd_nvram_map.h: ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.awk \
    ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.sh \
    ${SYSDIR}/dev/bhnd/nvram/nvram_map
bhnd_nvram_map.h:
	sh ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.sh \
	    ${SYSDIR}/dev/bhnd/nvram/nvram_map -h
.endif

.if !empty(SRCS:Mbhnd_nvram_map_data.h)
CLEANFILES+=	bhnd_nvram_map_data.h
bhnd_nvram_map_data.h: ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.awk \
    ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.sh \
    ${SYSDIR}/dev/bhnd/nvram/nvram_map
bhnd_nvram_map_data.h:
	sh ${SYSDIR}/dev/bhnd/tools/nvram_map_gen.sh \
	    ${SYSDIR}/dev/bhnd/nvram/nvram_map -d
.endif

.if !empty(SRCS:Musbdevs.h)
CLEANFILES+=	usbdevs.h
usbdevs.h: ${SYSDIR}/tools/usbdevs2h.awk ${SYSDIR}/dev/usb/usbdevs
	${AWK} -f ${SYSDIR}/tools/usbdevs2h.awk ${SYSDIR}/dev/usb/usbdevs -h
.endif

.if !empty(SRCS:Musbdevs_data.h)
CLEANFILES+=	usbdevs_data.h
usbdevs_data.h: ${SYSDIR}/tools/usbdevs2h.awk ${SYSDIR}/dev/usb/usbdevs
	${AWK} -f ${SYSDIR}/tools/usbdevs2h.awk ${SYSDIR}/dev/usb/usbdevs -d
.endif

.if !empty(SRCS:Macpi_quirks.h)
CLEANFILES+=	acpi_quirks.h
acpi_quirks.h: ${SYSDIR}/tools/acpi_quirks2h.awk ${SYSDIR}/dev/acpica/acpi_quirks
	${AWK} -f ${SYSDIR}/tools/acpi_quirks2h.awk ${SYSDIR}/dev/acpica/acpi_quirks
.endif

.if !empty(SRCS:Massym.inc) || !empty(DPSRCS:Massym.inc)
CLEANFILES+=	assym.inc
DEPENDOBJS+=	genassym.o
DPSRCS+=	offset.inc
.endif
.if defined(MODULE_TIED)
DPSRCS+=	offset.inc
.endif
.if !empty(SRCS:Moffset.inc) || !empty(DPSRCS:Moffset.inc)
CLEANFILES+=	offset.inc genoffset.o
DEPENDOBJS+=	genoffset.o
.endif
assym.inc: genassym.o
offset.inc: genoffset.o
assym.inc: ${SYSDIR}/kern/genassym.sh
	sh ${SYSDIR}/kern/genassym.sh genassym.o > ${.TARGET}
genassym.o: ${SYSDIR}/${MACHINE}/${MACHINE}/genassym.c offset.inc
genassym.o: ${SRCS:Mopt_*.h}
	${CC} -c ${CFLAGS:N-flto:N-fno-common} \
	    ${SYSDIR}/${MACHINE}/${MACHINE}/genassym.c
offset.inc: ${SYSDIR}/kern/genoffset.sh genoffset.o
	sh ${SYSDIR}/kern/genoffset.sh genoffset.o > ${.TARGET}
genoffset.o: ${SYSDIR}/kern/genoffset.c
genoffset.o: ${SRCS:Mopt_*.h}
	${CC} -c ${CFLAGS:N-flto:N-fno-common} \
	    ${SYSDIR}/kern/genoffset.c

CLEANDEPENDFILES+=	${_ILINKS}
# .depend needs include links so we remove them only together.
cleanilinks:
	rm -f ${_ILINKS}

OBJS_DEPEND_GUESS+= ${SRCS:M*.h}
.if defined(KERNBUILDDIR)
OBJS_DEPEND_GUESS+= opt_global.h
.endif

.include <bsd.dep.mk>
.include <bsd.clang-analyze.mk>
.include <bsd.obj.mk>
.include "kern.mk"
