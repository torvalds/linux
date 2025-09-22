/* Public domain. */

#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <linux/capability.h>
#include <linux/linkage.h>
#include <linux/uuid.h>
#include <linux/pid.h>
#include <linux/radix-tree.h>
#include <linux/wait_bit.h>
#include <linux/err.h>
#include <linux/sched/signal.h>	/* via percpu-rwsem.h -> rcuwait.h */
#include <linux/slab.h>

struct address_space;
struct seq_file;

struct file_operations {
	void *owner;
};

struct dentry {
};

#define DEFINE_SIMPLE_ATTRIBUTE(a, b, c, d)
#define MINORBITS	8

#endif
