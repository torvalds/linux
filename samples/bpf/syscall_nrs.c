#include <uapi/linux/unistd.h>
#include <linux/kbuild.h>

#define SYSNR(_NR) DEFINE(SYS ## _NR, _NR)

void syscall_defines(void)
{
	COMMENT("Linux system call numbers.");
	SYSNR(__NR_write);
	SYSNR(__NR_read);
	SYSNR(__NR_mmap);
}
