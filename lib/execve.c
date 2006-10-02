#include <asm/bug.h>
#include <asm/uaccess.h>

#define __KERNEL_SYSCALLS__
static int errno __attribute__((unused));
#include <asm/unistd.h>

#ifdef _syscall3
int kernel_execve (const char *filename, char *const argv[], char *const envp[])
								__attribute__((__weak__));
int kernel_execve (const char *filename, char *const argv[], char *const envp[])
{
	mm_segment_t fs = get_fs();
	int ret;

	WARN_ON(segment_eq(fs, USER_DS));
	ret = execve(filename, (char **)argv, (char **)envp);
	if (ret)
		ret = -errno;

	return ret;
}
#endif
