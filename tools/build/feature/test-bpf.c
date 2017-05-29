#include <asm/unistd.h>
#include <linux/bpf.h>
#include <unistd.h>

#ifndef __NR_bpf
# if defined(__i386__)
#  define __NR_bpf 357
# elif defined(__x86_64__)
#  define __NR_bpf 321
# elif defined(__aarch64__)
#  define __NR_bpf 280
# elif defined(__sparc__)
#  define __NR_bpf 349
# else
#  error __NR_bpf not defined. libbpf does not support your arch.
# endif
#endif

int main(void)
{
	union bpf_attr attr;

	/* Check fields in attr */
	attr.prog_type = BPF_PROG_TYPE_KPROBE;
	attr.insn_cnt = 0;
	attr.insns = 0;
	attr.license = 0;
	attr.log_buf = 0;
	attr.log_size = 0;
	attr.log_level = 0;
	attr.kern_version = 0;
	attr.prog_flags = 0;

	/*
	 * Test existence of __NR_bpf and BPF_PROG_LOAD.
	 * This call should fail if we run the testcase.
	 */
	return syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
}
