// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>

static int arc__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	arch->initialized = true;
	arch->objdump.comment_char = ';';
	arch->e_machine = EM_ARC;
	arch->e_flags = 0;
	return 0;
}
