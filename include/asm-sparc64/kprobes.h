#ifndef _SPARC64_KPROBES_H
#define _SPARC64_KPROBES_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/percpu.h>

typedef u32 kprobe_opcode_t;

#define BREAKPOINT_INSTRUCTION   0x91d02070 /* ta 0x70 */
#define BREAKPOINT_INSTRUCTION_2 0x91d02071 /* ta 0x71 */
#define MAX_INSN_SIZE 2

#define JPROBE_ENTRY(pentry)	(kprobe_opcode_t *)pentry
#define arch_remove_kprobe(p)	do {} while (0)

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
	unsigned long orig_tnpc;
	unsigned long orig_tstate_pil;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_orig_tnpc;
	unsigned long kprobe_orig_tstate_pil;
	long *jprobe_saved_esp;
	struct pt_regs jprobe_saved_regs;
	struct pt_regs *jprobe_saved_regs_location;
	struct sparc_stackf jprobe_saved_stack;
	struct prev_kprobe prev_kprobe;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
#endif /* _SPARC64_KPROBES_H */
