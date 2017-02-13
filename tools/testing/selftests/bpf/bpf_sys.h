#ifndef __BPF_SYS__
#define __BPF_SYS__

#include <stdint.h>
#include <stdlib.h>

#include <sys/syscall.h>

#include <linux/bpf.h>

static inline __u64 bpf_ptr_to_u64(const void *ptr)
{
	return (__u64)(unsigned long) ptr;
}

static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
#ifdef __NR_bpf
	return syscall(__NR_bpf, cmd, attr, size);
#else
	fprintf(stderr, "No bpf syscall, kernel headers too old?\n");
	errno = ENOSYS;
	return -1;
#endif
}

static inline int bpf_map_lookup(int fd, const void *key, void *value)
{
	union bpf_attr attr = {};

	attr.map_fd = fd;
	attr.key = bpf_ptr_to_u64(key);
	attr.value = bpf_ptr_to_u64(value);

	return bpf(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

static inline int bpf_map_update(int fd, const void *key, const void *value,
				 uint64_t flags)
{
	union bpf_attr attr = {};

	attr.map_fd = fd;
	attr.key = bpf_ptr_to_u64(key);
	attr.value = bpf_ptr_to_u64(value);
	attr.flags = flags;

	return bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

static inline int bpf_map_delete(int fd, const void *key)
{
	union bpf_attr attr = {};

	attr.map_fd = fd;
	attr.key = bpf_ptr_to_u64(key);

	return bpf(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

static inline int bpf_map_next_key(int fd, const void *key, void *next_key)
{
	union bpf_attr attr = {};

	attr.map_fd = fd;
	attr.key = bpf_ptr_to_u64(key);
	attr.next_key = bpf_ptr_to_u64(next_key);

	return bpf(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

static inline int bpf_map_create(enum bpf_map_type type, uint32_t size_key,
				 uint32_t size_value, uint32_t max_elem,
				 uint32_t flags)
{
	union bpf_attr attr = {};

	attr.map_type = type;
	attr.key_size = size_key;
	attr.value_size = size_value;
	attr.max_entries = max_elem;
	attr.map_flags = flags;

	return bpf(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static inline int bpf_prog_load(enum bpf_prog_type type,
				const struct bpf_insn *insns, size_t size_insns,
				const char *license, char *log, size_t size_log)
{
	union bpf_attr attr = {};

	attr.prog_type = type;
	attr.insns = bpf_ptr_to_u64(insns);
	attr.insn_cnt = size_insns / sizeof(struct bpf_insn);
	attr.license = bpf_ptr_to_u64(license);

	if (size_log > 0) {
		attr.log_buf = bpf_ptr_to_u64(log);
		attr.log_size = size_log;
		attr.log_level = 1;
		log[0] = 0;
	}

	return bpf(BPF_PROG_LOAD, &attr, sizeof(attr));
}

#endif /* __BPF_SYS__ */
