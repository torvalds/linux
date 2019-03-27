
# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

.PATH:	${SYSDIR}/dev/aic7xxx
KMOD=	ahc
SUBDIR+= ahc_isa ahc_pci

GENSRCS= aic7xxx_seq.h aic7xxx_reg.h
# AHC_REG_PRETTY_PRINT=1
REG_PRINT_OPT=
.ifdef AHC_REG_PRETTY_PRINT
GENSRCS+= aic7xxx_reg_print.c
CFLAGS+= -DAHC_REG_PRETTY_PRINT=1
REG_PRINT_OPT= -p aic7xxx_reg_print.c
.endif
BEFORE_DEPEND = ${GENSRCS}

../aicasm/aicasm: ${SYSDIR}/dev/aic7xxx/aicasm/*.[chyl]
	( cd ${.CURDIR}/../aicasm; ${MAKE} aicasm; )

.if make(ahcfirmware)
ahcfirmware: ${GENSRCS}
${GENSRCS}:								 \
		${SYSDIR}/dev/aic7xxx/aic7xxx.{reg,seq}	 \
		${SYSDIR}/cam/scsi/scsi_message.h 
	../aicasm/aicasm ${INCLUDES} -I${SYSDIR}/cam/scsi	 \
			-I${SYSDIR}/dev/aic7xxx		 \
			-o aic7xxx_seq.h -r aic7xxx_reg.h		 \
			${REG_PRINT_OPT}				 \
			-i ${SYSDIR}/dev/aic7xxx/aic7xxx_osm.h \
			${SYSDIR}/dev/aic7xxx/aic7xxx.seq
.elif defined(.MAKE.LEVEL)
# This target interfers with fmake's world view and causes this message
# to appear when building the tree from 8.x worlds where fmake is the
# default. fmake doens't define .MAKE.LEVEL so key off that to omit it,
# while still allowing more-modern makes to theoretically update things.
${GENSRCS}: .NOMETA
	@echo "Error: ${.TARGET} is missing.  Run 'make ahcfirmware'"
.endif


SRCS= ${GENSRCS}
SRCS+= aic7xxx.c aic7xxx_93cx6.c aic7xxx_osm.c aic7770.c
SRCS+= opt_scsi.h opt_aic7xxx.h opt_cam.h
SRCS+= device_if.h bus_if.h pci_if.h

.if make(cleanfirmware)
cleanfirmware: clean
CLEANFILES= ${GENSRCS}
.endif

.include <bsd.kmod.mk>
