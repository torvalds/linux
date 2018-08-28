// SPDX-License-Identifier: GPL-2.0
/*
 * Augment the openat syscall with the contents of the filename pointer argument.
 *
 * Test it with:
 *
 * perf trace -e tools/perf/examples/bpf/augmented_syscalls.c cat /etc/passwd > /dev/null
 *
 * It'll catch some openat syscalls related to the dynamic linked and
 * the last one should be the one for '/etc/passwd'.
 *
 * This matches what is marshalled into the raw_syscall:sys_enter payload
 * expected by the 'perf trace' beautifiers, and can be used by them unmodified,
 * which will be done as that feature is implemented in the next csets, for now
 * it will appear in a dump done by the default tracepoint handler in 'perf trace',
 * that uses bpf_output__fprintf() to just dump those contents, as done with
 * the bpf-output event associated with the __bpf_output__ map declared in
 * tools/perf/include/bpf/stdio.h.
 */

#include <stdio.h>
#include <linux/socket.h>

struct bpf_map SEC("maps") __augmented_syscalls__ = {
       .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
       .key_size = sizeof(int),
       .value_size = sizeof(u32),
       .max_entries = __NR_CPUS__,
};

struct augmented_filename {
	int	size;
	int	reserved;
	char	value[256];
};

#define augmented_filename_syscall_enter(syscall)						\
struct augmented_enter_##syscall##_args {			 				\
	struct syscall_enter_##syscall##_args	args;				 		\
	struct augmented_filename		filename;				 	\
};												\
int syscall_enter(syscall)(struct syscall_enter_##syscall##_args *args)				\
{												\
	struct augmented_enter_##syscall##_args augmented_args = { .filename.reserved = 0, }; 	\
	probe_read(&augmented_args.args, sizeof(augmented_args.args), args);			\
	augmented_args.filename.size = probe_read_str(&augmented_args.filename.value, 		\
						      sizeof(augmented_args.filename.value), 	\
						      args->filename_ptr); 			\
	perf_event_output(args, &__augmented_syscalls__, BPF_F_CURRENT_CPU, 			\
			  &augmented_args, 							\
			  (sizeof(augmented_args) - sizeof(augmented_args.filename.value) +	\
			   augmented_args.filename.size));					\
	return 0;										\
}

struct syscall_enter_openat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   dfd;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall_enter(openat);

struct syscall_enter_open_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	char		   *filename_ptr;
	long		   flags;
	long		   mode;
};

augmented_filename_syscall_enter(open);

struct syscall_enter_inotify_add_watch_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	char		   *filename_ptr;
	long		   mask;
};

augmented_filename_syscall_enter(inotify_add_watch);

struct statbuf;

struct syscall_enter_newstat_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	char		   *filename_ptr;
	struct stat	   *statbuf;
};

augmented_filename_syscall_enter(newstat);

#ifndef _K_SS_MAXSIZE
#define _K_SS_MAXSIZE 128
#endif

#define augmented_sockaddr_syscall_enter(syscall)						\
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
}

struct sockaddr;

struct syscall_enter_bind_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	struct sockaddr	   *addr_ptr;
	unsigned long	   addrlen;
};

augmented_sockaddr_syscall_enter(bind);

struct syscall_enter_connect_args {
	unsigned long long common_tp_fields;
	long		   syscall_nr;
	long		   fd;
	struct sockaddr	   *addr_ptr;
	unsigned long	   addrlen;
};

augmented_sockaddr_syscall_enter(connect);

license(GPL);
