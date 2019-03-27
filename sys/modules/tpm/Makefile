# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/tpm

KMOD=	tpm

SRCS=	tpm.c bus_if.h device_if.h
#Bus specific stuff.
SRCS+=	tpm_isa.c tpm_acpi.c isa_if.h opt_acpi.h acpi_if.h
#TPM 2.0
SRCS+=	tpm20.c tpm_crb.c tpm_tis.c

.include <bsd.kmod.mk>
