/*
 * net/nonet.c
 *
 * Dummy functions to allow us to configure network support entirely
 * out of the kernel.
 *
 * Distributed under the terms of the GNU GPL version 2.
 * Copyright (c) Matthew Wilcox 2003
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>

void __init sock_init(void)
{
	printk(KERN_INFO "Linux NoNET1.0 for Linux 2.6\n");
}

static int sock_no_open(struct inode *irrelevant, struct file *dontcare)
{
	return -ENXIO;
}

struct file_operations bad_sock_fops = {
	.owner = THIS_MODULE,
	.open = sock_no_open,
};
