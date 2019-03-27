# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/uart

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "i386"
uart_bus_acpi=uart_bus_acpi.c
.endif

.if ${MACHINE_CPUARCH} == "sparc64"
uart_bus_ebus=	uart_bus_ebus.c
.endif

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "arm" || \
    ${MACHINE_CPUARCH} == "powerpc" || ${MACHINE_CPUARCH} == "riscv" || \
    ${MACHINE_CPUARCH} == "sparc64"
ofw_bus_if=	ofw_bus_if.h
.endif

.if ${MACHINE} == "i386" || ${MACHINE} == "amd64"
_uart_cpu=uart_cpu_x86.c
.else
_uart_cpu=uart_cpu_${MACHINE}.c
.endif
.if exists(${.CURDIR:H:H}/dev/uart/${_uart_cpu})
uart_cpu_machine= ${_uart_cpu}
.endif

.if ${MACHINE} == "arm64"
uart_dev_mvebu=uart_dev_mvebu.c
uart_dev_mu=uart_dev_mu.c
.endif

KMOD=	uart
SRCS=	${uart_bus_acpi} ${uart_bus_ebus} uart_bus_isa.c uart_bus_pccard.c \
	uart_bus_pci.c uart_bus_puc.c uart_bus_scc.c \
	uart_core.c ${uart_cpu_machine} uart_dbg.c \
	${uart_dev_mvebu} uart_dev_ns8250.c ${uart_dev_mu} \
	uart_dev_quicc.c uart_dev_sab82532.c uart_dev_z8530.c \
	uart_if.c uart_if.h uart_subr.c uart_tty.c

SRCS+=	acpi_if.h bus_if.h card_if.h device_if.h isa_if.h ${ofw_bus_if} \
	pci_if.h \
	power_if.h pccarddevs.h serdev_if.h
SRCS+=	opt_acpi.h opt_platform.h opt_uart.h

.include <bsd.kmod.mk>
