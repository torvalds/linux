#ifndef _SPARC64_KPROBES_H
#define _SPARC64_KPROBES_H

#include <linux/config.h>
#include <linux/types.h>

typedef u32 kprobe_opcode_t;

#define BREAKPOINT_INSTRUCTION   0x91d02070 /* ta 0x70 */
#define BREAKPOINT_INSTRUCTION_2 0x91d02071 /* ta 0x71 */
#define MAX_INSN_SIZE 2

#define JPROBE_ENTRY(pentry)	(kprobe_opcode_t *)pentry

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

#ifdef CONFIG_KPROBES
extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
#else				/* !CONFIG_KPROBES */
static inline int kprobe_exceptions_notify(struct notifier_block *self,
					   unsigned long val, void *data)
{
	return 0;
}
#endif

#endif /* _SPARC64_KPROBES_H */
