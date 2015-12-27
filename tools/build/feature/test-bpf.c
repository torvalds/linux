#include <linux/bpf.h>

int main(void)
{
	union bpf_attr attr;

	attr.prog_type = BPF_PROG_TYPE_KPROBE;
	attr.insn_cnt = 0;
	attr.insns = 0;
	attr.license = 0;
	attr.log_buf = 0;
	attr.log_size = 0;
	attr.log_level = 0;
	attr.kern_version = 0;

	attr = attr;
	return 0;
}
