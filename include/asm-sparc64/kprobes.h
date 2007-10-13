#ifndef _SPARC64_KPROBES_H
#define _SPARC64_KPROBES_H

#include <linux/types.h>
#include <linux/percpu.h>

typedef u32 kprobe_opcode_t;

#define BREAKPOINT_INSTRUCTION   0x91d02070 /* ta 0x70 */
#define BREAKPOINT_INSTRUCTION_2 0x91d02071 /* ta 0x71 */
#define MAX_INSN_SIZE 2

#define arch_remove_kprobe(p)	do {} while (0)
#define  ARCH_INACTIVE_KPROBE_COUNT 0

#define flush_insn_slot(p)		\
do { 	flushi(&(p)->ainsn.insn[0]);	\
	flushi(&(p)->ainsn.insn[1]);	\
} while (0)

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long orig_tnpc;
	unsigned long orig_tstate_pil;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_orig_tnpc;
	unsigned long kprobe_orig_tstate_pil;
	struct pt_regs jprobe_saved_regs;
	struct prev_kprobe prev_kprobe;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
#endif /* _SPARC64_KPROBES_H */
