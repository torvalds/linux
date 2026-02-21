// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/zalloc.h>
#include "../disasm.h"

const struct arch *arch__new_arc(const struct e_machine_and_e_flags *id,
				 const char *cpuid __maybe_unused)
{
	struct arch *arch = zalloc(sizeof(*arch));

	if (!arch)
		return NULL;

	arch->name = "arc";
	arch->id = *id;
	arch->objdump.comment_char = ';';
	return arch;
}
