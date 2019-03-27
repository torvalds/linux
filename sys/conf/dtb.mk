# $FreeBSD$
#
# The include file <dtb.mk> handles building and installing dtb files.
#
# +++ variables +++
#
# DTC		The Device Tree Compiler to use
#
# DTS		List of the dts files to build and install.
#
# DTSO		List of the dts overlay files to build and install.
#
# DTBDIR	Base path for dtb modules [/boot/dtb]
#
# DTBOWN	.dtb file owner. [${BINOWN}]
#
# DTBGRP	.dtb file group. [${BINGRP}]
#
# DTBMODE	Module file mode. [${BINMODE}]
#
# DESTDIR	The tree where the module gets installed. [not set]
#
# +++ targets +++
#
#	install:
#               install the kernel module; if the Makefile
#               does not itself define the target install, the targets
#               beforeinstall and afterinstall may also be used to cause
#               actions immediately before and after the install target
#		is executed.
#

.include "dtb.build.mk"

.if !target(install) && !target(realinstall)
all: ${DTB} ${DTBO}
realinstall: _dtbinstall
.ORDER: beforeinstall _dtbinstall

CLEANFILES+=${DTB} ${DTBO}
.endif # !target(install) && !target(realinstall)

.include <bsd.dep.mk>
.include <bsd.obj.mk>
.include <bsd.links.mk>
