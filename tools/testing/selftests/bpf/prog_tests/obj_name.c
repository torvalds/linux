// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

void test_obj_name(void)
{
	struct {
		const char *name;
		int success;
		int expected_errno;
	} tests[] = {
		{ "", 1, 0 },
		{ "_123456789ABCDE", 1, 0 },
		{ "_123456789ABCDEF", 0, EINVAL },
		{ "_123456789ABCD\n", 0, EINVAL },
	};
	struct bpf_insn prog[] = {
		BPF_ALU64_IMM(BPF_MOV, BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	__u32 duration = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		size_t name_len = strlen(tests[i].name) + 1;
		union bpf_attr attr;
		size_t ncopy;
		int fd;

		/* test different attr.prog_name during BPF_PROG_LOAD */
		ncopy = name_len < sizeof(attr.prog_name) ?
			name_len : sizeof(attr.prog_name);
		bzero(&attr, sizeof(attr));
		attr.prog_type = BPF_PROG_TYPE_SCHED_CLS;
		attr.insn_cnt = 2;
		attr.insns = ptr_to_u64(prog);
		attr.license = ptr_to_u64("");
		memcpy(attr.prog_name, tests[i].name, ncopy);

		fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
		CHECK((tests[i].success && fd < 0) ||
		      (!tests[i].success && fd >= 0) ||
		      (!tests[i].success && errno != tests[i].expected_errno),
		      "check-bpf-prog-name",
		      "fd %d(%d) errno %d(%d)\n",
		       fd, tests[i].success, errno, tests[i].expected_errno);

		if (fd >= 0)
			close(fd);

		/* test different attr.map_name during BPF_MAP_CREATE */
		ncopy = name_len < sizeof(attr.map_name) ?
			name_len : sizeof(attr.map_name);
		bzero(&attr, sizeof(attr));
		attr.map_type = BPF_MAP_TYPE_ARRAY;
		attr.key_size = 4;
		attr.value_size = 4;
		attr.max_entries = 1;
		attr.map_flags = 0;
		memcpy(attr.map_name, tests[i].name, ncopy);
		fd = syscall(__NR_bpf, BPF_MAP_CREATE, &attr, sizeof(attr));
		CHECK((tests[i].success && fd < 0) ||
		      (!tests[i].success && fd >= 0) ||
		      (!tests[i].success && errno != tests[i].expected_errno),
		      "check-bpf-map-name",
		      "fd %d(%d) errno %d(%d)\n",
		      fd, tests[i].success, errno, tests[i].expected_errno);

		if (fd >= 0)
			close(fd);
	}
}
