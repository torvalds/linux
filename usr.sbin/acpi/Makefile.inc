# $Id: Makefile.inc,v 1.1 2000/07/14 18:16:22 iwasaki Exp $
# $FreeBSD$

ACPICA_DIR= ${SRCTOP}/sys/contrib/dev/acpica
CFLAGS+= -I${SRCTOP}/sys

PACKAGE=	acpi

.if exists(${.CURDIR:H:H}/Makefile.inc)
.include "${.CURDIR:H:H}/Makefile.inc"
.endif

.PATH:	${ACPICA_DIR}					\
	${ACPICA_DIR}/common				\
	${ACPICA_DIR}/compiler				\
	${ACPICA_DIR}/components/debugger		\
	${ACPICA_DIR}/components/disassembler		\
	${ACPICA_DIR}/components/dispatcher		\
	${ACPICA_DIR}/components/events			\
	${ACPICA_DIR}/components/executer		\
	${ACPICA_DIR}/components/hardware		\
	${ACPICA_DIR}/components/namespace		\
	${ACPICA_DIR}/components/parser			\
	${ACPICA_DIR}/components/resources		\
	${ACPICA_DIR}/components/tables			\
	${ACPICA_DIR}/components/utilities		\
	${ACPICA_DIR}/os_specific/service_layers
