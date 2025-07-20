#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/kexec.h>
#include <linux/reboot.h>
#include <sys/reboot.h>
#include <sys/syscall.h>

asm(
    "  .code64\n"
    "  .data\n"
    "purgatory_start:\n"

    // Trigger kexec debug exception handling
    "  int3\n"

    // Set load address for next time
    "  leaq purgatory_start_b(%rip), %r11\n"
    "  movq %r11, 8(%rsp)\n"

    // Back to Linux
    "  ret\n"

    // Same again
    "purgatory_start_b:\n"

    // Trigger kexec debug exception handling
    "  int3\n"

    // Set load address for next time
    "  leaq purgatory_start(%rip), %r11\n"
    "  movq %r11, 8(%rsp)\n"

    // Back to Linux
    "  ret\n"

    "purgatory_end:\n"
    ".previous"
);
extern char purgatory_start[], purgatory_end[];

int main (void)
{
        struct kexec_segment segment = {};
	int ret;

	segment.buf = purgatory_start;
	segment.bufsz = purgatory_end - purgatory_start;
	segment.mem = (void *)0x400000;
	segment.memsz = 0x1000;
	ret = syscall(__NR_kexec_load, 0x400000, 1, &segment, KEXEC_PRESERVE_CONTEXT);
	if (ret) {
		perror("kexec_load");
		exit(1);
	}

	ret = syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_KEXEC);
	if (ret) {
		perror("kexec reboot");
		exit(1);
	}

	ret = syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_KEXEC);
	if (ret) {
		perror("kexec reboot");
		exit(1);
	}
	printf("Success\n");
	return 0;
}

