
# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/aic7xxx
KMOD=	ahd

GENSRCS= aic79xx_seq.h aic79xx_reg.h
REG_PRINT_OPT=
# AHD_REG_PRETTY_PRINT=1
.ifdef AHD_REG_PRETTY_PRINT
GENSRCS += aic79xx_reg_print.c
CFLAGS+= -DAHD_REG_PRETTY_PRINT=1
REG_PRINT_OPT= -p aic79xx_reg_print.c
.endif
BEFORE_DEPEND= ${GENSRCS}

.if make(ahdfirmware)
ahdfirmware: ${GENSRCS}
${GENSRCS}:								 \
		${SRCTOP}/sys/dev/aic7xxx/aic79xx.{reg,seq}	 \
		${SRCTOP}/sys/cam/scsi/scsi_message.h
	aicasm ${INCLUDES} -I${SRCTOP}/sys/cam/scsi	 \
			-I${SRCTOP}/sys/dev/aic7xxx		 \
			-o aic79xx_seq.h -r aic79xx_reg.h		 \
			${REG_PRINT_OPT}				 \
			-i ${SRCTOP}/sys/dev/aic7xxx/aic79xx_osm.h \
			${SRCTOP}/sys/dev/aic7xxx/aic79xx.seq
.else
${GENSRCS}: .NOMETA
	@echo "Error: ${.TARGET} is missing.  Run 'make ahdfirmware'."
.endif


SRCS= ${GENSRCS}
SRCS+= aic79xx.c aic79xx_osm.c aic79xx_pci.c ahd_pci.c
SRCS+= opt_scsi.h opt_aic79xx.h opt_cam.h opt_ddb.h
SRCS+= device_if.h bus_if.h pci_if.h

.if make(cleanfirmware)
cleanfirmware: clean
CLEANFILES= ${GENSRCS}
.endif

.include <bsd.kmod.mk>

CWARNFLAGS.ahd_pci.c=	${NO_WCONSTANT_CONVERSION}
