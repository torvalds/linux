# $FreeBSD$

.include <bsd.init.mk>
# Grab all the options for a kernel build. For backwards compat, we need to
# do this after bsd.own.mk.
.include "kern.opts.mk"

DTC?=		dtc

.if !defined(SYSDIR)
.if defined(S)
SYSDIR=	${S}
.else
# Search for kernel source tree in standard places.
.for _dir in ${.CURDIR}/../.. ${.CURDIR}/../../.. /sys /usr/src/sys
.if exists(${_dir}/kern/)
SYSDIR=	${_dir:tA}
.endif
.endfor
.endif	# defined(S)
.endif	# defined(SYSDIR)

.if !defined(SYSDIR) || !exists(${SYSDIR}/kern/)
.error "can't find kernel source tree"
.endif

DTB=${DTS:T:R:S/$/.dtb/}
DTBO=${DTSO:T:R:S/$/.dtbo/}

.SUFFIXES: .dtb .dts .dtbo .dtso
.PATH.dts: ${SYSDIR}/gnu/dts/${MACHINE} ${SYSDIR}/dts/${MACHINE}
.PATH.dtso: ${SYSDIR}/dts/${MACHINE}/overlays

.export DTC ECHO

.dts.dtb:	${OP_META}
	@${ECHO} Generating ${.TARGET} from ${.IMPSRC}
	@${SYSDIR}/tools/fdt/make_dtb.sh ${SYSDIR} ${.IMPSRC} ${.OBJDIR}

.dtso.dtbo:	${OP_META}
	@${ECHO} Generating ${.TARGET} from ${.IMPSRC}
	@${SYSDIR}/tools/fdt/make_dtbo.sh ${SYSDIR} ${.IMPSRC} ${.OBJDIR}

# Add dependencies on the source file so that out-of-tree things can be included
# without any .PATH additions.
.for _dts in ${DTS}
${_dts:R:T}.dtb: ${_dts}
.endfor

.for _dtso in ${DTSO}
${_dtso:R:T}.dtbo: ${_dtso}
.endfor

_dtbinstall:
# Need to create this because installkernel doesn't invoke mtree with BSD.root.mtree
# to make sure the tree is setup properly. We don't recreate it to avoid duplicate
# entries in the NO_ROOT case.
	test -d ${DESTDIR}${DTBDIR} || ${INSTALL} -d -o ${DTBOWN} -g ${DTBGRP} ${DESTDIR}${DTBDIR}
.for _dtb in ${DTB}
.if ${MACHINE_CPUARCH} == "aarch64"
	# :H:T here to grab the vendor component of the DTB path in a way that
	# allows out-of-tree DTS builds, too.  We make the assumption that
	# out-of-tree DTS will have a similar directory structure to in-tree,
	# with .dts files appearing in a vendor/ directory.
	test -d ${DESTDIR}${DTBDIR}/${_dtb:H:T} || ${INSTALL} -d -o ${DTBOWN} -g ${DTBGRP} ${DESTDIR}${DTBDIR}/${_dtb:H:T}
	${INSTALL} -o ${DTBOWN} -g ${DTBGRP} -m ${DTBMODE} \
	    ${_INSTALLFLAGS} ${_dtb:T} ${DESTDIR}${DTBDIR}/${_dtb:H:T}
.else
	${INSTALL} -o ${DTBOWN} -g ${DTBGRP} -m ${DTBMODE} \
	    ${_INSTALLFLAGS} ${_dtb} ${DESTDIR}${DTBDIR}/
.endif
.endfor
	test -d ${DESTDIR}${DTBODIR} || ${INSTALL} -d -o ${DTBOWN} -g ${DTBGRP} ${DESTDIR}${DTBODIR}
.for _dtbo in ${DTBO}
	${INSTALL} -o ${DTBOWN} -g ${DTBGRP} -m ${DTBMODE} \
	    ${_INSTALLFLAGS} ${_dtbo} ${DESTDIR}${DTBODIR}/
.endfor
