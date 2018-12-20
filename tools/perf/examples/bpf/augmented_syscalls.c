// SPDX-License-Identifier: GPL-2.0
/*
 * Augment syscalls with the contents of the pointer arguments.
 *
 * Test it with:
 *
 * perf trace -e tools/perf/examples/bpf/augmented_syscalls.c cat /etc/passwd > /dev/null
 *
 * It'll catch some openat syscalls related to the dynamic linked and
 * the last one should be the one for '/etc/passwd'.
 *
 * This matches what is marshalled into the raw_syscall:sys_enter payload
 * expected by the 'perf trace' beautifiers, and can be used by them, that will
 * check if perf_sample->raw_data is more than what is expected for each
 * syscalls:sys_{enter,exit}_SYSCALL tracepoint, uing the extra data as the
 * contents of pointer arguments.
 */

#include <stdio.h>
#include <linux/socket.h>

struct bpf_map SEC("maps") __augmented_syscalls__ = {
       .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
       .key_size = sizeof(int),
       .value_size = sizeof(u32),
       .max_entries = __NR_CPUS__,
};

struct syscall_exit_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   ret;
};

struct augmented_filename {
	unsigned int	size;
	int		reserved;
	char		value[256];
};

#define augmented_filename_syscall(syscall)							\
struct augmented_enter_##syscall##_args {			 				\
	struct syscall_enter_##syscall##_args	args;				 		\
	struct augmented_filename		filename;				 	\
};												\
int syscall_enter(syscall)(struct syscall_enter_##syscall##_args *args)				\
{												\
	struct augmented_enter_##syscall##_args augmented_args = { .filename.reserved = 0, }; 	\
	unsigned int len = sizeof(augmented_args);						\
	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);			\
	augmented_args.filename.size = probe_read_str(&augmented_args.filename.value, 		\
						      sizeof(augmented_args.filename.value), 	\
						      args->filename_ptr); 			\
	if (augmented_args.filename.size < sizeof(augmented_args.filename.value)) {		\
		len -= sizeof(augmented_args.filename.value) - augmented_args.filename.size;	\
		len &= sizeof(augmented_args.filename.value) - 1;				\
	}											\
	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, 			\
			  &augmented_args, len);						\
	return 0;										\
}												\
int syscall_exit(syscall)(struct syscall_exit_args *args)					\
{												\
       return 1; /* 0 as soon as we start copying data returned by the kernel, e.g. 'read' */	\
}

struct syscall_enter_openat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   dfd;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall(openat);

struct syscall_enter_open_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall(open);

struct syscall_enter_inotify_add_watch_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	char		   *filename_ptr;
	long		   mask;
};

augmented_filename_syscall(inotify_add_watch);

struct statbuf;

struct syscall_enter_newstat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	char		   *filename_ptr;
	struct stat	   *statbuf;
};

augmented_filename_syscall(newstat);

#ifndef _K_SS_MAXSIZE
#define _K_SS_MAXSIZE 128
#endif

#define augmented_sockaddr_syscall(syscall)						\
struct augmented_enter_##syscall##_args {			 				\
	struct syscall_enter_##syscall##_args	args;				 		\
	struct sockaddr_storage			addr;						\
};												\
int syscall_enter(syscall)(struct syscall_enter_##syscall##_args *args)				\
{												\
	struct augmented_enter_##syscall##_args augmented_args;				 	\
	unsigned long addrlen = sizeof(augmented_args.addr);					\
	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);			\
/* FIXME_CLANG_OPTIMIZATION_THAT_ACCESSES_USER_CONTROLLED_ADDRLEN_DESPITE_THIS_CHECK */		\
/*	if (addrlen > augmented_args.args.addrlen)				     */		\
/*		addrlen = augmented_args.args.addrlen;				     */		\
/*										     */		\
	probe_read(&augmented_args.addr, addrlen, args->addr_ptr); 				\
	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, 			\
			  &augmented_args, 							\
			  sizeof(augmented_args) - sizeof(augmented_args.addr) + addrlen);	\
	return 0;										\
}												\
int syscall_exit(syscall)(struct syscall_exit_args *args)					\
{												\
       return 1; /* 0 as soon as we start copying data returned by the kernel, e.g. 'read' */	\
}

struct sockaddr;

struct syscall_enter_bind_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	struct sockaddr	   *addr_ptr;
	unsigned long	   addrlen;
};

augmented_sockaddr_syscall(bind);

struct syscall_enter_connect_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	struct sockaddr	   *addr_ptr;
	unsigned long	   addrlen;
};

augmented_sockaddr_syscall(connect);

struct syscall_enter_sendto_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	void		   *buff;
	long		   len;
	unsigned long	   flags;
	struct sockaddr	   *addr_ptr;
	long		   addr_len;
};

augmented_sockaddr_syscall(sendto);

license(GPL);
