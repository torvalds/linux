#
# $FreeBSD$
#

_ALL!=	cd ${.CURDIR} && ls Doxyfile-*
ALL=	${_ALL:C/Doxyfile-//g}
PDF_ALL=${_ALL:C/Doxyfile/pdf/g}

TARGET_ARCH?=	${MACHINE_ARCH}
S?=/usr/src/sys
LOCALBASE?=/usr/local

MFILES!= find ${S} -name \*.m | sed -e 's:${S}/::g'
HFILES=	${MFILES:T:S/.m$/.h/}
AWK?=	awk

.MAIN:	usage

usage:
	@echo "Possible targets are:"
.for entry in ${ALL}
	@echo "	${entry}"
	@echo "	pdf-${entry}"
	@echo "	clean-${entry}"
.endfor
	@echo
	@echo "	all"
	@echo "	pdf-all"
	@echo "	clean"

all:	${ALL}
pdf-all:${PDF_ALL}

mfiles: ${HFILES:S/^/${.OBJDIR}\/include\//}

DOXYGEN_DEST_PATH?=	${.OBJDIR}
DOXYGEN_LATEX_DEST_PATH?=${.OBJDIR}
DOXYGEN_PDF_DEST_PATH?=	${.OBJDIR}

.if exists(${S}/${TARGET_ARCH}/linux)
DOXYGEN_LINUX_PATH=	${S}/${TARGET_ARCH}/linux
.endif
.if exists(${S}/${TARGET_ARCH}/linux32)
DOXYGEN_LINUX_PATH+=	${S}/${TARGET_ARCH}/linux32
.endif

#
# generate the necessary targets
#
.for target in ${ALL}
${target}:	mfiles ${.OBJDIR}/${target}/${target}.tag

${.OBJDIR}/${target}/${target}.tag:
	@mkdir -p ${.OBJDIR}/${target}
	@cd ${.OBJDIR} && \
		env DOXYGEN_INCLUDE_PATH=${.CURDIR} \
		    DOXYGEN_SRC_PATH=${S}  \
		    DOXYGEN_DEST_PATH=${DOXYGEN_DEST_PATH} \
		    DOXYGEN_SRC_INCLUDE_PATH="${S}/sys ${S}/../include ${S}/${TARGET_ARCH}/include ${.OBJDIR}/include" \
		    DOXYGEN_TARGET_ARCH=${TARGET_ARCH} \
		    DOXYGEN_LINUX_PATH="${DOXYGEN_LINUX_PATH}" \
		    NOTREVIEWED=${.CURDIR}/notreviewed.dox \
		    PATH=${LOCALBASE}/bin:${PATH} \
			doxygen ${.CURDIR}/Doxyfile-${target}
	@echo "API docs for ${target} are now available in ${DOXYGEN_DEST_PATH}/${target}/." | /usr/bin/fmt

pdf-${target}:	${.OBJDIR}/${target}/${target}.tag
	@cd ${DOXYGEN_LATEX_DEST_PATH}/${target}/latex && ${MAKE} refman.pdf && cp refman.pdf ${DOXYGEN_PDF_DEST_PATH}/${target}.pdf
	@echo "API docs for ${target} are now available in ${DOXYGEN_PDF_DEST_PATH}/." | /usr/bin/fmt

CLEANDIRS+=	${DOXYGEN_DEST_PATH}/${target}
CLEANDIRS+=	${.OBJDIR}/${target}/
CLEANFILES+=	${DOXYGEN_PDF_DEST_PATH}/${target}.pdf
clean-${target}:
	rm -rf ${DOXYGEN_DEST_PATH}/${target} ${.OBJDIR}/${target}
.endfor

CLEANDIRS+=	${.OBJDIR}/include
.for file in ${MFILES}
CLEANFILES+=     ${.OBJDIR}/include/${file:T:S/.m$/.h/}
${.OBJDIR}/include/${file:T:S/.m$/.h/}: ${S}/${file}
	@mkdir -p ${.OBJDIR}/include
	cd ${.OBJDIR}/include && ${AWK} -f $S/tools/makeobjops.awk ${S}/${file} -h
.endfor

#
# update target for the config files {config syntax change}
#
update-doxys:
.for config_file in ${_ALL}
	@doxygen -u ${config_file}
.endfor

.include "Dependencies"
.include <bsd.obj.mk>
