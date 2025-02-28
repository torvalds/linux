// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel headers useful to build tracing programs
 * such as for running eBPF tracing tools.
 *
 * (Borrowed code from kernel/configs.c)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/init.h>

/*
 * Define kernel_headers_data and kernel_headers_data_end, within which the
 * compressed kernel headers are stored. The file is first compressed with xz.
 */

asm (
"	.pushsection .rodata, \"a\"		\n"
"	.global kernel_headers_data		\n"
"kernel_headers_data:				\n"
"	.incbin \"kernel/kheaders_data.tar.xz\"	\n"
"	.global kernel_headers_data_end		\n"
"kernel_headers_data_end:			\n"
"	.popsection				\n"
);

extern char kernel_headers_data[];
extern char kernel_headers_data_end[];

static struct bin_attribute kheaders_attr __ro_after_init =
	__BIN_ATTR_SIMPLE_RO(kheaders.tar.xz, 0444);

static int __init ikheaders_init(void)
{
	kheaders_attr.private = kernel_headers_data;
	kheaders_attr.size = (kernel_headers_data_end -
			      kernel_headers_data);
	return sysfs_create_bin_file(kernel_kobj, &kheaders_attr);
}

static void __exit ikheaders_cleanup(void)
{
	sysfs_remove_bin_file(kernel_kobj, &kheaders_attr);
}

module_init(ikheaders_init);
module_exit(ikheaders_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joel Fernandes");
MODULE_DESCRIPTION("Echo the kernel header artifacts used to build the kernel");
