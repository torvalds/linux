#ifndef _LINUX_NS_COMMON_H
#define _LINUX_NS_COMMON_H

struct proc_ns_operations;

struct ns_common {
	const struct proc_ns_operations *ops;
	unsigned int inum;
};

#endif
