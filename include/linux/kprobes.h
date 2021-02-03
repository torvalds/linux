/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/linux/kprobes.h
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 * 2005-May	Hien Nguyen <hien@us.ibm.com> and Jim Keniston
 *		<jkenisto@us.ibm.com>  and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> added function-return probes.
 */
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/bug.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>
#include <linux/refcount.h>
#include <linux/freelist.h>
#include <asm/kprobes.h>

#ifdef CONFIG_KPROBES

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002
#define KPROBE_REENTER		0x00000004
#define KPROBE_HIT_SSDONE	0x00000008

#else /* CONFIG_KPROBES */
#include <asm-generic/kprobes.h>
typedef int kprobe_opcode_t;
struct arch_specific_insn {
	int dummy;
};
#endif /* CONFIG_KPROBES */

struct kprobe;
struct pt_regs;
struct kretprobe;
struct kretprobe_instance;
typedef int (*kprobe_pre_handler_t) (struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t) (struct kprobe *, struct pt_regs *,
				       unsigned long flags);
typedef int (*kprobe_fault_handler_t) (struct kprobe *, struct pt_regs *,
				       int trapnr);
typedef int (*kretprobe_handler_t) (struct kretprobe_instance *,
				    struct pt_regs *);

struct kprobe {
	struct hlist_node hlist;

	/* list of kprobes for multi-handler support */
	struct list_head list;

	/*count the number of times this probe was temporarily disarmed */
	unsigned long nmissed;

	/* location of the probe point */
	kprobe_opcode_t *addr;

	/* Allow user to indicate symbol name of the probe point */
	const char *symbol_name;

	/* Offset into the symbol */
	unsigned int offset;

	/* Called before addr is executed. */
	kprobe_pre_handler_t pre_handler;

	/* Called after addr is executed, unless... */
	kprobe_post_handler_t post_handler;

	/*
	 * ... called if executing addr causes a fault (eg. page fault).
	 * Return 1 if it handled fault, otherwise kernel will see it.
	 */
	kprobe_fault_handler_t fault_handler;

	/* Saved opcode (which has been replaced with breakpoint) */
	kprobe_opcode_t opcode;

	/* copy of the original instruction */
	struct arch_specific_insn ainsn;

	/*
	 * Indicates various status flags.
	 * Protected by kprobe_mutex after this kprobe is registered.
	 */
	u32 flags;
};

/* Kprobe status flags */
#define KPROBE_FLAG_GONE	1 /* breakpoint has already gone */
#define KPROBE_FLAG_DISABLED	2 /* probe is temporarily disabled */
#define KPROBE_FLAG_OPTIMIZED	4 /*
				   * probe is really optimized.
				   * NOTE:
				   * this flag is only for optimized_kprobe.
				   */
#define KPROBE_FLAG_FTRACE	8 /* probe is using ftrace */

/* Has this kprobe gone ? */
static inline int kprobe_gone(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_GONE;
}

/* Is this kprobe disabled ? */
static inline int kprobe_disabled(struct kprobe *p)
{
	return p->flags & (KPROBE_FLAG_DISABLED | KPROBE_FLAG_GONE);
}

/* Is this kprobe really running optimized path ? */
static inline int kprobe_optimized(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_OPTIMIZED;
}

/* Is this kprobe uses ftrace ? */
static inline int kprobe_ftrace(struct kprobe *p)
{
	return p->flags & KPROBE_FLAG_FTRACE;
}

/*
 * Function-return probe -
 * Note:
 * User needs to provide a handler function, and initialize maxactive.
 * maxactive - The maximum number of instances of the probed function that
 * can be active concurrently.
 * nmissed - tracks the number of times the probed function's return was
 * ignored, due to maxactive being too low.
 *
 */
struct kretprobe_holder {
	struct kretprobe	*rp;
	refcount_t		ref;
};

struct kretprobe {
	struct kprobe kp;
	kretprobe_handler_t handler;
	kretprobe_handler_t entry_handler;
	int maxactive;
	int nmissed;
	size_t data_size;
	struct freelist_head freelist;
	struct kretprobe_holder *rph;
};

struct kretprobe_instance {
	union {
		struct freelist_node freelist;
		struct rcu_head rcu;
	};
	struct llist_node llist;
	struct kretprobe_holder *rph;
	kprobe_opcode_t *ret_addr;
	void *fp;
	char data[];
};

struct kretprobe_blackpoint {
	const char *name;
	void *addr;
};

struct kprobe_blacklist_entry {
	struct list_head list;
	unsigned long start_addr;
	unsigned long end_addr;
};

#ifdef CONFIG_KPROBES
DECLARE_PER_CPU(struct kprobe *, current_kprobe);
DECLARE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

/*
 * For #ifdef avoidance:
 */
static inline int kprobes_built_in(void)
{
	return 1;
}

extern void kprobe_busy_begin(void);
extern void kprobe_busy_end(void);

#ifdef CONFIG_KRETPROBES
extern void arch_prepare_kretprobe(struct kretprobe_instance *ri,
				   struct pt_regs *regs);
extern int arch_trampoline_kprobe(struct kprobe *p);

/* If the trampoline handler called from a kprobe, use this version */
unsigned long __kretprobe_trampoline_handler(struct pt_regs *regs,
				void *trampoline_address,
				void *frame_pointer);

static nokprobe_inline
unsigned long kretprobe_trampoline_handler(struct pt_regs *regs,
				void *trampoline_address,
				void *frame_pointer)
{
	unsigned long ret;
	/*
	 * Set a dummy kprobe for avoiding kretprobe recursion.
	 * Since kretprobe never runs in kprobe handler, no kprobe must
	 * be running at this point.
	 */
	kprobe_busy_begin();
	ret = __kretprobe_trampoline_handler(regs, trampoline_address, frame_pointer);
	kprobe_busy_end();

	return ret;
}

static nokprobe_inline struct kretprobe *get_kretprobe(struct kretprobe_instance *ri)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_any_held(),
		"Kretprobe is accessed from instance under preemptive context");

	return READ_ONCE(ri->rph->rp);
}

#else /* CONFIG_KRETPROBES */
static inline void arch_prepare_kretprobe(struct kretprobe *rp,
					struct pt_regs *regs)
{
}
static inline int arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}
#endif /* CONFIG_KRETPROBES */

extern struct kretprobe_blackpoint kretprobe_blacklist[];

#ifdef CONFIG_KPROBES_SANITY_TEST
extern int init_test_probes(void);
#else
static inline int init_test_probes(void)
{
	return 0;
}
#endif /* CONFIG_KPROBES_SANITY_TEST */

extern int arch_prepare_kprobe(struct kprobe *p);
extern void arch_arm_kprobe(struct kprobe *p);
extern void arch_disarm_kprobe(struct kprobe *p);
extern int arch_init_kprobes(void);
extern void kprobes_inc_nmissed_count(struct kprobe *p);
extern bool arch_within_kprobe_blacklist(unsigned long addr);
extern int arch_populate_kprobe_blacklist(void);
extern bool arch_kprobe_on_func_entry(unsigned long offset);
extern int kprobe_on_func_entry(kprobe_opcode_t *addr, const char *sym, unsigned long offset);

extern bool within_kprobe_blacklist(unsigned long addr);
extern int kprobe_add_ksym_blacklist(unsigned long entry);
extern int kprobe_add_area_blacklist(unsigned long start, unsigned long end);

struct kprobe_insn_cache {
	struct mutex mutex;
	void *(*alloc)(void);	/* allocate insn page */
	void (*free)(void *);	/* free insn page */
	const char *sym;	/* symbol for insn pages */
	struct list_head pages; /* list of kprobe_insn_page */
	size_t insn_size;	/* size of instruction slot */
	int nr_garbage;
};

#ifdef __ARCH_WANT_KPROBES_INSN_SLOT
extern kprobe_opcode_t *__get_insn_slot(struct kprobe_insn_cache *c);
extern void __free_insn_slot(struct kprobe_insn_cache *c,
			     kprobe_opcode_t *slot, int dirty);
/* sleep-less address checking routine  */
extern bool __is_insn_slot_addr(struct kprobe_insn_cache *c,
				unsigned long addr);

#define DEFINE_INSN_CACHE_OPS(__name)					\
extern struct kprobe_insn_cache kprobe_##__name##_slots;		\
									\
static inline kprobe_opcode_t *get_##__name##_slot(void)		\
{									\
	return __get_insn_slot(&kprobe_##__name##_slots);		\
}									\
									\
static inline void free_##__name##_slot(kprobe_opcode_t *slot, int dirty)\
{									\
	__free_insn_slot(&kprobe_##__name##_slots, slot, dirty);	\
}									\
									\
static inline bool is_kprobe_##__name##_slot(unsigned long addr)	\
{									\
	return __is_insn_slot_addr(&kprobe_##__name##_slots, addr);	\
}
#define KPROBE_INSN_PAGE_SYM		"kprobe_insn_page"
#define KPROBE_OPTINSN_PAGE_SYM		"kprobe_optinsn_page"
int kprobe_cache_get_kallsym(struct kprobe_insn_cache *c, unsigned int *symnum,
			     unsigned long *value, char *type, char *sym);
#else /* __ARCH_WANT_KPROBES_INSN_SLOT */
#define DEFINE_INSN_CACHE_OPS(__name)					\
static inline bool is_kprobe_##__name##_slot(unsigned long addr)	\
{									\
	return 0;							\
}
#endif

DEFINE_INSN_CACHE_OPS(insn);

#ifdef CONFIG_OPTPROBES
/*
 * Internal structure for direct jump optimized probe
 */
struct optimized_kprobe {
	struct kprobe kp;
	struct list_head list;	/* list for optimizing queue */
	struct arch_optimized_insn optinsn;
};

/* Architecture dependent functions for direct jump optimization */
extern int arch_prepared_optinsn(struct arch_optimized_insn *optinsn);
extern int arch_check_optimized_kprobe(struct optimized_kprobe *op);
extern int arch_prepare_optimized_kprobe(struct optimized_kprobe *op,
					 struct kprobe *orig);
extern void arch_remove_optimized_kprobe(struct optimized_kprobe *op);
extern void arch_optimize_kprobes(struct list_head *oplist);
extern void arch_unoptimize_kprobes(struct list_head *oplist,
				    struct list_head *done_list);
extern void arch_unoptimize_kprobe(struct optimized_kprobe *op);
extern int arch_within_optimized_kprobe(struct optimized_kprobe *op,
					unsigned long addr);

extern void opt_pre_handler(struct kprobe *p, struct pt_regs *regs);

DEFINE_INSN_CACHE_OPS(optinsn);

#ifdef CONFIG_SYSCTL
extern int sysctl_kprobes_optimization;
extern int proc_kprobes_optimization_handler(struct ctl_table *table,
					     int write, void *buffer,
					     size_t *length, loff_t *ppos);
#endif
extern void wait_for_kprobe_optimizer(void);
#else
static inline void wait_for_kprobe_optimizer(void) { }
#endif /* CONFIG_OPTPROBES */
#ifdef CONFIG_KPROBES_ON_FTRACE
extern void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *ops, struct ftrace_regs *fregs);
extern int arch_prepare_kprobe_ftrace(struct kprobe *p);
#endif

int arch_check_ftrace_location(struct kprobe *p);

/* Get the kprobe at this addr (if any) - called with preemption disabled */
struct kprobe *get_kprobe(void *addr);

/* kprobe_running() will just return the current_kprobe on this CPU */
static inline struct kprobe *kprobe_running(void)
{
	return (__this_cpu_read(current_kprobe));
}

static inline void reset_current_kprobe(void)
{
	__this_cpu_write(current_kprobe, NULL);
}

static inline struct kprobe_ctlblk *get_kprobe_ctlblk(void)
{
	return this_cpu_ptr(&kprobe_ctlblk);
}

kprobe_opcode_t *kprobe_lookup_name(const char *name, unsigned int offset);
int register_kprobe(struct kprobe *p);
void unregister_kprobe(struct kprobe *p);
int register_kprobes(struct kprobe **kps, int num);
void unregister_kprobes(struct kprobe **kps, int num);
unsigned long arch_deref_entry_point(void *);

int register_kretprobe(struct kretprobe *rp);
void unregister_kretprobe(struct kretprobe *rp);
int register_kretprobes(struct kretprobe **rps, int num);
void unregister_kretprobes(struct kretprobe **rps, int num);

void kprobe_flush_task(struct task_struct *tk);

void kprobe_free_init_mem(void);

int disable_kprobe(struct kprobe *kp);
int enable_kprobe(struct kprobe *kp);

void dump_kprobe(struct kprobe *kp);

void *alloc_insn_page(void);
void free_insn_page(void *page);

int kprobe_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
		       char *sym);

int arch_kprobe_get_kallsym(unsigned int *symnum, unsigned long *value,
			    char *type, char *sym);
#else /* !CONFIG_KPROBES: */

static inline int kprobes_built_in(void)
{
	return 0;
}
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	return 0;
}
static inline struct kprobe *get_kprobe(void *addr)
{
	return NULL;
}
static inline struct kprobe *kprobe_running(void)
{
	return NULL;
}
static inline int register_kprobe(struct kprobe *p)
{
	return -ENOSYS;
}
static inline int register_kprobes(struct kprobe **kps, int num)
{
	return -ENOSYS;
}
static inline void unregister_kprobe(struct kprobe *p)
{
}
static inline void unregister_kprobes(struct kprobe **kps, int num)
{
}
static inline int register_kretprobe(struct kretprobe *rp)
{
	return -ENOSYS;
}
static inline int register_kretprobes(struct kretprobe **rps, int num)
{
	return -ENOSYS;
}
static inline void unregister_kretprobe(struct kretprobe *rp)
{
}
static inline void unregister_kretprobes(struct kretprobe **rps, int num)
{
}
static inline void kprobe_flush_task(struct task_struct *tk)
{
}
static inline void kprobe_free_init_mem(void)
{
}
static inline int disable_kprobe(struct kprobe *kp)
{
	return -ENOSYS;
}
static inline int enable_kprobe(struct kprobe *kp)
{
	return -ENOSYS;
}

static inline bool within_kprobe_blacklist(unsigned long addr)
{
	return true;
}
static inline int kprobe_get_kallsym(unsigned int symnum, unsigned long *value,
				     char *type, char *sym)
{
	return -ERANGE;
}
#endif /* CONFIG_KPROBES */
static inline int disable_kretprobe(struct kretprobe *rp)
{
	return disable_kprobe(&rp->kp);
}
static inline int enable_kretprobe(struct kretprobe *rp)
{
	return enable_kprobe(&rp->kp);
}

#ifndef CONFIG_KPROBES
static inline bool is_kprobe_insn_slot(unsigned long addr)
{
	return false;
}
#endif
#ifndef CONFIG_OPTPROBES
static inline bool is_kprobe_optinsn_slot(unsigned long addr)
{
	return false;
}
#endif

/* Returns true if kprobes handled the fault */
static nokprobe_inline bool kprobe_page_fault(struct pt_regs *regs,
					      unsigned int trap)
{
	if (!kprobes_built_in())
		return false;
	if (user_mode(regs))
		return false;
	/*
	 * To be potentially processing a kprobe fault and to be allowed
	 * to call kprobe_running(), we have to be non-preemptible.
	 */
	if (preemptible())
		return false;
	if (!kprobe_running())
		return false;
	return kprobe_fault_handler(regs, trap);
}

#endif /* _LINUX_KPROBES_H */
