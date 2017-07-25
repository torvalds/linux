/*
 * bpf-script-test-prologue.c
 * Test BPF prologue
 */
#ifndef LINUX_VERSION_CODE
# error Need LINUX_VERSION_CODE
# error Example: for 4.2 kernel, put 'clang-opt="-DLINUX_VERSION_CODE=0x40200" into llvm section of ~/.perfconfig'
#endif
#define SEC(NAME) __attribute__((section(NAME), used))

#include <uapi/linux/fs.h>

/*
 * If CONFIG_PROFILE_ALL_BRANCHES is selected,
 * 'if' is redefined after include kernel header.
 * Recover 'if' for BPF object code.
 */
#ifdef if
# undef if
#endif

#define FMODE_READ		0x1
#define FMODE_WRITE		0x2

static void (*bpf_trace_printk)(const char *fmt, int fmt_size, ...) =
	(void *) 6;

SEC("func=null_lseek file->f_mode offset orig")
int bpf_func__null_lseek(void *ctx, int err, unsigned long f_mode,
			 unsigned long offset, unsigned long orig)
{
	if (err)
		return 0;
	if (f_mode & FMODE_WRITE)
		return 0;
	if (offset & 1)
		return 0;
	if (orig == SEEK_CUR)
		return 0;
	return 1;
}

char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;
