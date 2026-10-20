// SPDX-License-Identifier: GPL-2.0
/*
 * A switch dense enough that Clang builds a jump table for it, in a section of
 * its own: .rodata..Lswitch.table.<function>.
 *
 * The table belongs to the function and has to travel with it.  It is named
 * after the function but is not part of it, so klp diff has to associate the
 * two rather than treating the table as unrelated data.
 *
 * The patch adds a case, which changes the table's contents and length.
 */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";
const char *status_to_string(unsigned int c)
{
	switch (c) {
	case 0: return "idle";
	case 1: return "running";
	case 2: return "stopped";
	case 3: return "error";
	case 4: return "paused";
	case 5: return "waiting";
	case 6: return "starting";
	case 7: return "stopping";
#ifdef PATCHED
	case 8: return "completed";
#endif
	}
	return "unknown";
}
