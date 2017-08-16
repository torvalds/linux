/*
 * bpf-script-test-kbuild.c
 * Test include from kernel header
 */
#ifndef LINUX_VERSION_CODE
# error Need LINUX_VERSION_CODE
# error Example: for 4.2 kernel, put 'clang-opt="-DLINUX_VERSION_CODE=0x40200" into llvm section of ~/.perfconfig'
#endif
#define SEC(NAME) __attribute__((section(NAME), used))

#include <uapi/linux/fs.h>
#include <uapi/asm/ptrace.h>

SEC("func=vfs_llseek")
int bpf_func__vfs_llseek(void *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;
