#include <linux/export.h>
#include <linux/init.h>

	__INITRODATA

	.globl VMLINUX_SYMBOL(system_certificate_list)
VMLINUX_SYMBOL(system_certificate_list):
	.incbin "kernel/x509_certificate_list"
	.globl VMLINUX_SYMBOL(system_certificate_list_end)
VMLINUX_SYMBOL(system_certificate_list_end):
