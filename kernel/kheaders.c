// SPDX-License-Identifier: GPL-2.0
/*
 * Provide kernel headers useful to build tracing programs
 * such as for running eBPF tracing tools.
 *
 * (Borrowed code from kernel/configs.c)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/uaccess.h>

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

extern char kernel_headers_data;
extern char kernel_headers_data_end;

static ssize_t
ikheaders_read_current(struct file *file, char __user *buf,
		      size_t len, loff_t *offset)
{
	return simple_read_from_buffer(buf, len, offset,
				       &kernel_headers_data,
				       &kernel_headers_data_end -
				       &kernel_headers_data);
}

static const struct file_operations ikheaders_file_ops = {
	.read = ikheaders_read_current,
	.llseek = default_llseek,
};

static int __init ikheaders_init(void)
{
	struct proc_dir_entry *entry;

	/* create the current headers file */
	entry = proc_create("kheaders.tar.xz", S_IRUGO, NULL,
			    &ikheaders_file_ops);
	if (!entry)
		return -ENOMEM;

	proc_set_size(entry,
		      &kernel_headers_data_end -
		      &kernel_headers_data);
	return 0;
}

static void __exit ikheaders_cleanup(void)
{
	remove_proc_entry("kheaders.tar.xz", NULL);
}

module_init(ikheaders_init);
module_exit(ikheaders_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joel Fernandes");
MODULE_DESCRIPTION("Echo the kernel header artifacts used to build the kernel");
