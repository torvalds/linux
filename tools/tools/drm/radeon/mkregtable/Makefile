# $FreeBSD$

all: regtables

PROG=	mkregtable

SRCS=	mkregtable.c

MAN=

MKREGTABLE=	${PROG}
KERNSRCDIR=	${.CURDIR}/../../../../../sys
REG_SRCS_DIR=	${KERNSRCDIR}/dev/drm2/radeon/reg_srcs
REG_DEST_DIR=	${KERNSRCDIR}/dev/drm2/radeon

regtables:					\
	${REG_DEST_DIR}/rn50_reg_safe.h		\
	${REG_DEST_DIR}/r100_reg_safe.h		\
	${REG_DEST_DIR}/r200_reg_safe.h		\
	${REG_DEST_DIR}/rv515_reg_safe.h	\
	${REG_DEST_DIR}/r300_reg_safe.h		\
	${REG_DEST_DIR}/r420_reg_safe.h		\
	${REG_DEST_DIR}/rs600_reg_safe.h	\
	${REG_DEST_DIR}/r600_reg_safe.h		\
	${REG_DEST_DIR}/evergreen_reg_safe.h	\
	${REG_DEST_DIR}/cayman_reg_safe.h

${REG_DEST_DIR}/rn50_reg_safe.h: ${REG_SRCS_DIR}/rn50 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/rn50 > $@

${REG_DEST_DIR}/r100_reg_safe.h: ${REG_SRCS_DIR}/r100 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/r100 > $@

${REG_DEST_DIR}/r200_reg_safe.h: ${REG_SRCS_DIR}/r200 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/r200 > $@

${REG_DEST_DIR}/rv515_reg_safe.h: ${REG_SRCS_DIR}/rv515 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/rv515 > $@

${REG_DEST_DIR}/r300_reg_safe.h: ${REG_SRCS_DIR}/r300 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/r300 > $@

${REG_DEST_DIR}/r420_reg_safe.h: ${REG_SRCS_DIR}/r420 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/r420 > $@

${REG_DEST_DIR}/rs600_reg_safe.h: ${REG_SRCS_DIR}/rs600 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/rs600 > $@

${REG_DEST_DIR}/r600_reg_safe.h: ${REG_SRCS_DIR}/r600 ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/r600 > $@

${REG_DEST_DIR}/evergreen_reg_safe.h: ${REG_SRCS_DIR}/evergreen ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/evergreen > $@

${REG_DEST_DIR}/cayman_reg_safe.h: ${REG_SRCS_DIR}/cayman ${MKREGTABLE}
	./${MKREGTABLE} ${REG_SRCS_DIR}/cayman > $@

.include <bsd.prog.mk>
