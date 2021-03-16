#ifndef _KDBPRIVATE_H
#define _KDBPRIVATE_H

/*
 * Kernel Debugger Architecture Independent Private Headers
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/kgdb.h>
#include "../debug_core.h"

/* Kernel Debugger Command codes.  Must not overlap with error codes. */
#define KDB_CMD_GO	(-1001)
#define KDB_CMD_CPU	(-1002)
#define KDB_CMD_SS	(-1003)
#define KDB_CMD_KGDB (-1005)

/* Internal debug flags */
#define KDB_DEBUG_FLAG_BP	0x0002	/* Breakpoint subsystem debug */
#define KDB_DEBUG_FLAG_BB_SUMM	0x0004	/* Basic block analysis, summary only */
#define KDB_DEBUG_FLAG_AR	0x0008	/* Activation record, generic */
#define KDB_DEBUG_FLAG_ARA	0x0010	/* Activation record, arch specific */
#define KDB_DEBUG_FLAG_BB	0x0020	/* All basic block analysis */
#define KDB_DEBUG_FLAG_STATE	0x0040	/* State flags */
#define KDB_DEBUG_FLAG_MASK	0xffff	/* All debug flags */
#define KDB_DEBUG_FLAG_SHIFT	16	/* Shift factor for dbflags */

#define KDB_DEBUG(flag)	(kdb_flags & \
	(KDB_DEBUG_FLAG_##flag << KDB_DEBUG_FLAG_SHIFT))
#define KDB_DEBUG_STATE(text, value) if (KDB_DEBUG(STATE)) \
		kdb_print_state(text, value)

#if BITS_PER_LONG == 32

#define KDB_PLATFORM_ENV	"BYTESPERWORD=4"

#define kdb_machreg_fmt		"0x%lx"
#define kdb_machreg_fmt0	"0x%08lx"
#define kdb_bfd_vma_fmt		"0x%lx"
#define kdb_bfd_vma_fmt0	"0x%08lx"
#define kdb_elfw_addr_fmt	"0x%x"
#define kdb_elfw_addr_fmt0	"0x%08x"
#define kdb_f_count_fmt		"%d"

#elif BITS_PER_LONG == 64

#define KDB_PLATFORM_ENV	"BYTESPERWORD=8"

#define kdb_machreg_fmt		"0x%lx"
#define kdb_machreg_fmt0	"0x%016lx"
#define kdb_bfd_vma_fmt		"0x%lx"
#define kdb_bfd_vma_fmt0	"0x%016lx"
#define kdb_elfw_addr_fmt	"0x%x"
#define kdb_elfw_addr_fmt0	"0x%016x"
#define kdb_f_count_fmt		"%ld"

#endif

/*
 * KDB_MAXBPT describes the total number of breakpoints
 * supported by this architecure.
 */
#define KDB_MAXBPT	16

/* Symbol table format returned by kallsyms. */
typedef struct __ksymtab {
		unsigned long value;	/* Address of symbol */
		const char *mod_name;	/* Module containing symbol or
					 * "kernel" */
		unsigned long mod_start;
		unsigned long mod_end;
		const char *sec_name;	/* Section containing symbol */
		unsigned long sec_start;
		unsigned long sec_end;
		const char *sym_name;	/* Full symbol name, including
					 * any version */
		unsigned long sym_start;
		unsigned long sym_end;
		} kdb_symtab_t;
extern int kallsyms_symbol_next(char *prefix_name, int flag, int buf_size);
extern int kallsyms_symbol_complete(char *prefix_name, int max_len);

/* Exported Symbols for kernel loadable modules to use. */
extern int kdb_getarea_size(void *, unsigned long, size_t);
extern int kdb_putarea_size(unsigned long, void *, size_t);

/*
 * Like get_user and put_user, kdb_getarea and kdb_putarea take variable
 * names, not pointers.  The underlying *_size functions take pointers.
 */
#define kdb_getarea(x, addr) kdb_getarea_size(&(x), addr, sizeof((x)))
#define kdb_putarea(addr, x) kdb_putarea_size(addr, &(x), sizeof((x)))

extern int kdb_getphysword(unsigned long *word,
			unsigned long addr, size_t size);
extern int kdb_getword(unsigned long *, unsigned long, size_t);
extern int kdb_putword(unsigned long, unsigned long, size_t);

extern int kdbgetularg(const char *, unsigned long *);
extern int kdbgetu64arg(const char *, u64 *);
extern char *kdbgetenv(const char *);
extern int kdbgetaddrarg(int, const char **, int*, unsigned long *,
			 long *, char **);
extern int kdbgetsymval(const char *, kdb_symtab_t *);
extern int kdbnearsym(unsigned long, kdb_symtab_t *);
extern void kdbnearsym_cleanup(void);
extern char *kdb_strdup(const char *str, gfp_t type);
extern void kdb_symbol_print(unsigned long, const kdb_symtab_t *, unsigned int);

/* Routine for debugging the debugger state. */
extern void kdb_print_state(const char *, int);

extern int kdb_state;
#define KDB_STATE_KDB		0x00000001	/* Cpu is inside kdb */
#define KDB_STATE_LEAVING	0x00000002	/* Cpu is leaving kdb */
#define KDB_STATE_CMD		0x00000004	/* Running a kdb command */
#define KDB_STATE_KDB_CONTROL	0x00000008	/* This cpu is under
						 * kdb control */
#define KDB_STATE_HOLD_CPU	0x00000010	/* Hold this cpu inside kdb */
#define KDB_STATE_DOING_SS	0x00000020	/* Doing ss command */
#define KDB_STATE_SSBPT		0x00000080	/* Install breakpoint
						 * after one ss, independent of
						 * DOING_SS */
#define KDB_STATE_REENTRY	0x00000100	/* Valid re-entry into kdb */
#define KDB_STATE_SUPPRESS	0x00000200	/* Suppress error messages */
#define KDB_STATE_PAGER		0x00000400	/* pager is available */
#define KDB_STATE_GO_SWITCH	0x00000800	/* go is switching
						 * back to initial cpu */
#define KDB_STATE_WAIT_IPI	0x00002000	/* Waiting for kdb_ipi() NMI */
#define KDB_STATE_RECURSE	0x00004000	/* Recursive entry to kdb */
#define KDB_STATE_IP_ADJUSTED	0x00008000	/* Restart IP has been
						 * adjusted */
#define KDB_STATE_GO1		0x00010000	/* go only releases one cpu */
#define KDB_STATE_KEYBOARD	0x00020000	/* kdb entered via
						 * keyboard on this cpu */
#define KDB_STATE_KEXEC		0x00040000	/* kexec issued */
#define KDB_STATE_DOING_KGDB	0x00080000	/* kgdb enter now issued */
#define KDB_STATE_KGDB_TRANS	0x00200000	/* Transition to kgdb */
#define KDB_STATE_ARCH		0xff000000	/* Reserved for arch
						 * specific use */

#define KDB_STATE(flag) (kdb_state & KDB_STATE_##flag)
#define KDB_STATE_SET(flag) ((void)(kdb_state |= KDB_STATE_##flag))
#define KDB_STATE_CLEAR(flag) ((void)(kdb_state &= ~KDB_STATE_##flag))

extern int kdb_nextline; /* Current number of lines displayed */

typedef struct _kdb_bp {
	unsigned long	bp_addr;	/* Address breakpoint is present at */
	unsigned int	bp_free:1;	/* This entry is available */
	unsigned int	bp_enabled:1;	/* Breakpoint is active in register */
	unsigned int	bp_type:4;	/* Uses hardware register */
	unsigned int	bp_installed:1;	/* Breakpoint is installed */
	unsigned int	bp_delay:1;	/* Do delayed bp handling */
	unsigned int	bp_delayed:1;	/* Delayed breakpoint */
	unsigned int	bph_length;	/* HW break length */
} kdb_bp_t;

#ifdef CONFIG_KGDB_KDB
extern kdb_bp_t kdb_breakpoints[/* KDB_MAXBPT */];

/* The KDB shell command table */
typedef struct _kdbtab {
	char    *cmd_name;		/* Command name */
	kdb_func_t cmd_func;		/* Function to execute command */
	char    *cmd_usage;		/* Usage String for this command */
	char    *cmd_help;		/* Help message for this command */
	short    cmd_minlen;		/* Minimum legal # command
					 * chars required */
	kdb_cmdflags_t cmd_flags;	/* Command behaviour flags */
} kdbtab_t;

extern int kdb_bt(int, const char **);	/* KDB display back trace */

/* KDB breakpoint management functions */
extern void kdb_initbptab(void);
extern void kdb_bp_install(struct pt_regs *);
extern void kdb_bp_remove(void);

typedef enum {
	KDB_DB_BPT,	/* Breakpoint */
	KDB_DB_SS,	/* Single-step trap */
	KDB_DB_SSBPT,	/* Single step over breakpoint */
	KDB_DB_NOBPT	/* Spurious breakpoint */
} kdb_dbtrap_t;

extern int kdb_main_loop(kdb_reason_t, kdb_reason_t,
			 int, kdb_dbtrap_t, struct pt_regs *);

/* Miscellaneous functions and data areas */
extern int kdb_grepping_flag;
#define KDB_GREPPING_FLAG_SEARCH 0x8000
extern char kdb_grep_string[];
#define KDB_GREP_STRLEN 256
extern int kdb_grep_leading;
extern int kdb_grep_trailing;
extern char *kdb_cmds[];
extern unsigned long kdb_task_state_string(const char *);
extern char kdb_task_state_char (const struct task_struct *);
extern unsigned long kdb_task_state(const struct task_struct *p,
				    unsigned long mask);
extern void kdb_ps_suppressed(void);
extern void kdb_ps1(const struct task_struct *p);
extern void kdb_print_nameval(const char *name, unsigned long val);
extern void kdb_send_sig(struct task_struct *p, int sig);
extern void kdb_meminfo_proc_show(void);
extern char kdb_getchar(void);
extern char *kdb_getstr(char *, size_t, const char *);
extern void kdb_gdb_state_pass(char *buf);

/* Defines for kdb_symbol_print */
#define KDB_SP_SPACEB	0x0001		/* Space before string */
#define KDB_SP_SPACEA	0x0002		/* Space after string */
#define KDB_SP_PAREN	0x0004		/* Parenthesis around string */
#define KDB_SP_VALUE	0x0008		/* Print the value of the address */
#define KDB_SP_SYMSIZE	0x0010		/* Print the size of the symbol */
#define KDB_SP_NEWLINE	0x0020		/* Newline after string */
#define KDB_SP_DEFAULT (KDB_SP_VALUE|KDB_SP_PAREN)

#define KDB_TSK(cpu) kgdb_info[cpu].task
#define KDB_TSKREGS(cpu) kgdb_info[cpu].debuggerinfo

extern struct task_struct *kdb_curr_task(int);

#define kdb_task_has_cpu(p) (task_curr(p))

#define GFP_KDB (in_dbg_master() ? GFP_ATOMIC : GFP_KERNEL)

extern void *debug_kmalloc(size_t size, gfp_t flags);
extern void debug_kfree(void *);
extern void debug_kusage(void);

extern struct task_struct *kdb_current_task;
extern struct pt_regs *kdb_current_regs;

#ifdef CONFIG_KDB_KEYBOARD
extern void kdb_kbd_cleanup_state(void);
#else /* ! CONFIG_KDB_KEYBOARD */
#define kdb_kbd_cleanup_state()
#endif /* ! CONFIG_KDB_KEYBOARD */

#ifdef CONFIG_MODULES
extern struct list_head *kdb_modules;
#endif /* CONFIG_MODULES */

extern char kdb_prompt_str[];

#define	KDB_WORD_SIZE	((int)sizeof(unsigned long))

#endif /* CONFIG_KGDB_KDB */

#define kdb_func_printf(format, args...) \
	kdb_printf("%s: " format, __func__, ## args)

#define kdb_dbg_printf(mask, format, args...) \
	do { \
		if (KDB_DEBUG(mask)) \
			kdb_func_printf(format, ## args); \
	} while (0)

#endif	/* !_KDBPRIVATE_H */
