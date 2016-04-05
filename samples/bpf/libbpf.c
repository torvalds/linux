/* eBPF mini library */
#include <stdlib.h>
#include <stdio.h>
#include <linux/unistd.h>
#include <unistd.h>
#include <string.h>
#include <linux/netlink.h>
#include <linux/bpf.h>
#include <errno.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#include "libbpf.h"

static __u64 ptr_to_u64(void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

int bpf_create_map(enum bpf_map_type map_type, int key_size, int value_size,
		   int max_entries, int map_flags)
{
	union bpf_attr attr = {
		.map_type = map_type,
		.key_size = key_size,
		.value_size = value_size,
		.max_entries = max_entries,
		.map_flags = map_flags,
	};

	return syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
}

int bpf_update_elem(int fd, void *key, void *value, unsigned long long flags)
{
	union bpf_attr attr = {
		.map_fd = fd,
		.key = ptr_to_u64(key),
		.value = ptr_to_u64(value),
		.flags = flags,
	};

	return syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

int bpf_lookup_elem(int fd, void *key, void *value)
{
	union bpf_attr attr = {
		.map_fd = fd,
		.key = ptr_to_u64(key),
		.value = ptr_to_u64(value),
	};

	return syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

int bpf_delete_elem(int fd, void *key)
{
	union bpf_attr attr = {
		.map_fd = fd,
		.key = ptr_to_u64(key),
	};

	return syscall(__NR_bpf, BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

int bpf_get_next_key(int fd, void *key, void *next_key)
{
	union bpf_attr attr = {
		.map_fd = fd,
		.key = ptr_to_u64(key),
		.next_key = ptr_to_u64(next_key),
	};

	return syscall(__NR_bpf, BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

#define ROUND_UP(x, n) (((x) + (n) - 1u) & ~((n) - 1u))

char bpf_log_buf[LOG_BUF_SIZE];

int bpf_prog_load(enum bpf_prog_type prog_type,
		  const struct bpf_insn *insns, int prog_len,
		  const char *license, int kern_version)
{
	union bpf_attr attr = {
		.prog_type = prog_type,
		.insns = ptr_to_u64((void *) insns),
		.insn_cnt = prog_len / sizeof(struct bpf_insn),
		.license = ptr_to_u64((void *) license),
		.log_buf = ptr_to_u64(bpf_log_buf),
		.log_size = LOG_BUF_SIZE,
		.log_level = 1,
	};

	/* assign one field outside of struct init to make sure any
	 * padding is zero initialized
	 */
	attr.kern_version = kern_version;

	bpf_log_buf[0] = 0;

	return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}

int bpf_obj_pin(int fd, const char *pathname)
{
	union bpf_attr attr = {
		.pathname	= ptr_to_u64((void *)pathname),
		.bpf_fd		= fd,
	};

	return syscall(__NR_bpf, BPF_OBJ_PIN, &attr, sizeof(attr));
}

int bpf_obj_get(const char *pathname)
{
	union bpf_attr attr = {
		.pathname	= ptr_to_u64((void *)pathname),
	};

	return syscall(__NR_bpf, BPF_OBJ_GET, &attr, sizeof(attr));
}

int open_raw_sock(const char *name)
{
	struct sockaddr_ll sll;
	int sock;

	sock = socket(PF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
	if (sock < 0) {
		printf("cannot create raw socket\n");
		return -1;
	}

	memset(&sll, 0, sizeof(sll));
	sll.sll_family = AF_PACKET;
	sll.sll_ifindex = if_nametoindex(name);
	sll.sll_protocol = htons(ETH_P_ALL);
	if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
		printf("bind to %s: %s\n", name, strerror(errno));
		close(sock);
		return -1;
	}

	return sock;
}

int perf_event_open(struct perf_event_attr *attr, int pid, int cpu,
		    int group_fd, unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu,
		       group_fd, flags);
}
