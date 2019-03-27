# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

SUBDIR=	geom_bde \
	geom_cache \
	geom_concat \
	geom_eli \
	geom_flashmap \
	geom_gate \
	geom_journal \
	geom_label \
	geom_linux_lvm \
	geom_mirror \
	geom_mountver \
	geom_multipath \
	geom_nop \
	geom_part \
	geom_raid \
	geom_raid3 \
	geom_sched \
	geom_shsec \
	geom_stripe \
	geom_uzip \
	geom_vinum \
	geom_virstor \
	geom_zero

.if ${MK_CCD} != "no" || defined(ALL_MODULES)
SUBDIR+=	geom_ccd
.endif

.include <bsd.subdir.mk>
