#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/unistd.h>

#include <bpf/bpf.h>

#define LOG_SIZE (1 << 20)

#define err(str...)	printf("ERROR: " str)

static const struct bpf_insn code_sample[] = {
	/* We need a few instructions to pass the min log length */
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_MOV64_IMM(BPF_REG_0, 0),
	BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0,
		     BPF_FUNC_map_lookup_elem),
	BPF_EXIT_INSN(),
};

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

static int load(char *log, size_t log_len, int log_level)
{
	union bpf_attr attr;

	bzero(&attr, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insn_cnt = (__u32)(sizeof(code_sample) / sizeof(struct bpf_insn));
	attr.insns = ptr_to_u64(code_sample);
	attr.license = ptr_to_u64("GPL");
	attr.log_buf = ptr_to_u64(log);
	attr.log_size = log_len;
	attr.log_level = log_level;

	return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}

static void check_ret(int ret, int exp_errno)
{
	if (ret > 0) {
		close(ret);
		err("broken sample loaded successfully!?\n");
		exit(1);
	}

	if (!ret || errno != exp_errno) {
		err("Program load returned: ret:%d/errno:%d, expected ret:%d/errno:%d\n",
		    ret, errno, -1, exp_errno);
		exit(1);
	}
}

static void check_ones(const char *buf, size_t len, const char *msg)
{
	while (len--)
		if (buf[len] != 1) {
			err("%s", msg);
			exit(1);
		}
}

static void test_log_good(char *log, size_t buf_len, size_t log_len,
			  size_t exp_len, int exp_errno, const char *full_log)
{
	size_t len;
	int ret;

	memset(log, 1, buf_len);

	ret = load(log, log_len, 1);
	check_ret(ret, exp_errno);

	len = strnlen(log, buf_len);
	if (len == buf_len) {
		err("verifier did not NULL terminate the log\n");
		exit(1);
	}
	if (exp_len && len != exp_len) {
		err("incorrect log length expected:%zd have:%zd\n",
		    exp_len, len);
		exit(1);
	}

	if (strchr(log, 1)) {
		err("verifier leaked a byte through\n");
		exit(1);
	}

	check_ones(log + len + 1, buf_len - len - 1,
		   "verifier wrote bytes past NULL termination\n");

	if (memcmp(full_log, log, LOG_SIZE)) {
		err("log did not match expected output\n");
		exit(1);
	}
}

static void test_log_bad(char *log, size_t log_len, int log_level)
{
	int ret;

	ret = load(log, log_len, log_level);
	check_ret(ret, EINVAL);
	if (log)
		check_ones(log, LOG_SIZE,
			   "verifier touched log with bad parameters\n");
}

int main(int argc, char **argv)
{
	struct rlimit limit  = { RLIM_INFINITY, RLIM_INFINITY };
	char full_log[LOG_SIZE];
	char log[LOG_SIZE];
	size_t want_len;
	int i;

	/* allow unlimited locked memory to have more consistent error code */
	if (setrlimit(RLIMIT_MEMLOCK, &limit) < 0)
		perror("Unable to lift memlock rlimit");

	memset(log, 1, LOG_SIZE);

	/* Test incorrect attr */
	printf("Test log_level 0...\n");
	test_log_bad(log, LOG_SIZE, 0);

	printf("Test log_size < 128...\n");
	test_log_bad(log, 15, 1);

	printf("Test log_buff = NULL...\n");
	test_log_bad(NULL, LOG_SIZE, 1);

	/* Test with log big enough */
	printf("Test oversized buffer...\n");
	test_log_good(full_log, LOG_SIZE, LOG_SIZE, 0, EACCES, full_log);

	want_len = strlen(full_log);

	printf("Test exact buffer...\n");
	test_log_good(log, LOG_SIZE, want_len + 2, want_len, EACCES, full_log);

	printf("Test undersized buffers...\n");
	for (i = 0; i < 64; i++) {
		full_log[want_len - i + 1] = 1;
		full_log[want_len - i] = 0;

		test_log_good(log, LOG_SIZE, want_len + 1 - i, want_len - i,
			      ENOSPC, full_log);
	}

	printf("test_verifier_log: OK\n");
	return 0;
}
