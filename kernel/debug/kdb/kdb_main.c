/*
 * Kernel Debugger Architecture Independent Main Code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 * Xscale (R) modifications copyright (C) 2003 Intel Corporation.
 * Copyright (c) 2009 Wind River Systems, Inc.  All Rights Reserved.
 */

#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/stat.h>
#include <linux/sched/debug.h>
#include <linux/sysrq.h>
#include <linux/smp.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/atomic.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kgdb.h>
#include <linux/kdb.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/nmi.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/kdebug.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/security.h>
#include "kdb_private.h"

#undef	MODULE_PARAM_PREFIX
#define	MODULE_PARAM_PREFIX "kdb."

static int kdb_cmd_enabled = CONFIG_KDB_DEFAULT_ENABLE;
module_param_named(cmd_enable, kdb_cmd_enabled, int, 0600);

char kdb_grep_string[KDB_GREP_STRLEN];
int kdb_grepping_flag;
EXPORT_SYMBOL(kdb_grepping_flag);
int kdb_grep_leading;
int kdb_grep_trailing;

/*
 * Kernel debugger state flags
 */
unsigned int kdb_flags;

/*
 * kdb_lock protects updates to kdb_initial_cpu.  Used to
 * single thread processors through the kernel debugger.
 */
int kdb_initial_cpu = -1;	/* cpu number that owns kdb */
int kdb_nextline = 1;
int kdb_state;			/* General KDB state */

struct task_struct *kdb_current_task;
struct pt_regs *kdb_current_regs;

const char *kdb_diemsg;
static int kdb_go_count;
#ifdef CONFIG_KDB_CONTINUE_CATASTROPHIC
static unsigned int kdb_continue_catastrophic =
	CONFIG_KDB_CONTINUE_CATASTROPHIC;
#else
static unsigned int kdb_continue_catastrophic;
#endif

/* kdb_cmds_head describes the available commands. */
static LIST_HEAD(kdb_cmds_head);

typedef struct _kdbmsg {
	int	km_diag;	/* kdb diagnostic */
	char	*km_msg;	/* Corresponding message text */
} kdbmsg_t;

#define KDBMSG(msgnum, text) \
	{ KDB_##msgnum, text }

static kdbmsg_t kdbmsgs[] = {
	KDBMSG(NOTFOUND, "Command Not Found"),
	KDBMSG(ARGCOUNT, "Improper argument count, see usage."),
	KDBMSG(BADWIDTH, "Illegal value for BYTESPERWORD use 1, 2, 4 or 8, "
	       "8 is only allowed on 64 bit systems"),
	KDBMSG(BADRADIX, "Illegal value for RADIX use 8, 10 or 16"),
	KDBMSG(NOTENV, "Cannot find environment variable"),
	KDBMSG(NOENVVALUE, "Environment variable should have value"),
	KDBMSG(NOTIMP, "Command not implemented"),
	KDBMSG(ENVFULL, "Environment full"),
	KDBMSG(ENVBUFFULL, "Environment buffer full"),
	KDBMSG(TOOMANYBPT, "Too many breakpoints defined"),
#ifdef CONFIG_CPU_XSCALE
	KDBMSG(TOOMANYDBREGS, "More breakpoints than ibcr registers defined"),
#else
	KDBMSG(TOOMANYDBREGS, "More breakpoints than db registers defined"),
#endif
	KDBMSG(DUPBPT, "Duplicate breakpoint address"),
	KDBMSG(BPTNOTFOUND, "Breakpoint not found"),
	KDBMSG(BADMODE, "Invalid IDMODE"),
	KDBMSG(BADINT, "Illegal numeric value"),
	KDBMSG(INVADDRFMT, "Invalid symbolic address format"),
	KDBMSG(BADREG, "Invalid register name"),
	KDBMSG(BADCPUNUM, "Invalid cpu number"),
	KDBMSG(BADLENGTH, "Invalid length field"),
	KDBMSG(NOBP, "No Breakpoint exists"),
	KDBMSG(BADADDR, "Invalid address"),
	KDBMSG(NOPERM, "Permission denied"),
};
#undef KDBMSG

static const int __nkdb_err = ARRAY_SIZE(kdbmsgs);


/*
 * Initial environment.   This is all kept static and local to
 * this file.   We don't want to rely on the memory allocation
 * mechanisms in the kernel, so we use a very limited allocate-only
 * heap for new and altered environment variables.  The entire
 * environment is limited to a fixed number of entries (add more
 * to __env[] if required) and a fixed amount of heap (add more to
 * KDB_ENVBUFSIZE if required).
 */

static char *__env[31] = {
#if defined(CONFIG_SMP)
	"PROMPT=[%d]kdb> ",
#else
	"PROMPT=kdb> ",
#endif
	"MOREPROMPT=more> ",
	"RADIX=16",
	"MDCOUNT=8",		/* lines of md output */
	KDB_PLATFORM_ENV,
	"DTABCOUNT=30",
	"NOSECT=1",
};

static const int __nenv = ARRAY_SIZE(__env);

struct task_struct *kdb_curr_task(int cpu)
{
	struct task_struct *p = curr_task(cpu);
#ifdef	_TIF_MCA_INIT
	if ((task_thread_info(p)->flags & _TIF_MCA_INIT) && KDB_TSK(cpu))
		p = krp->p;
#endif
	return p;
}

/*
 * Update the permissions flags (kdb_cmd_enabled) to match the
 * current lockdown state.
 *
 * Within this function the calls to security_locked_down() are "lazy". We
 * avoid calling them if the current value of kdb_cmd_enabled already excludes
 * flags that might be subject to lockdown. Additionally we deliberately check
 * the lockdown flags independently (even though read lockdown implies write
 * lockdown) since that results in both simpler code and clearer messages to
 * the user on first-time debugger entry.
 *
 * The permission masks during a read+write lockdown permits the following
 * flags: INSPECT, SIGNAL, REBOOT (and ALWAYS_SAFE).
 *
 * The INSPECT commands are not blocked during lockdown because they are
 * not arbitrary memory reads. INSPECT covers the backtrace family (sometimes
 * forcing them to have no arguments) and lsmod. These commands do expose
 * some kernel state but do not allow the developer seated at the console to
 * choose what state is reported. SIGNAL and REBOOT should not be controversial,
 * given these are allowed for root during lockdown already.
 */
static void kdb_check_for_lockdown(void)
{
	const int write_flags = KDB_ENABLE_MEM_WRITE |
				KDB_ENABLE_REG_WRITE |
				KDB_ENABLE_FLOW_CTRL;
	const int read_flags = KDB_ENABLE_MEM_READ |
			       KDB_ENABLE_REG_READ;

	bool need_to_lockdown_write = false;
	bool need_to_lockdown_read = false;

	if (kdb_cmd_enabled & (KDB_ENABLE_ALL | write_flags))
		need_to_lockdown_write =
			security_locked_down(LOCKDOWN_DBG_WRITE_KERNEL);

	if (kdb_cmd_enabled & (KDB_ENABLE_ALL | read_flags))
		need_to_lockdown_read =
			security_locked_down(LOCKDOWN_DBG_READ_KERNEL);

	/* De-compose KDB_ENABLE_ALL if required */
	if (need_to_lockdown_write || need_to_lockdown_read)
		if (kdb_cmd_enabled & KDB_ENABLE_ALL)
			kdb_cmd_enabled = KDB_ENABLE_MASK & ~KDB_ENABLE_ALL;

	if (need_to_lockdown_write)
		kdb_cmd_enabled &= ~write_flags;

	if (need_to_lockdown_read)
		kdb_cmd_enabled &= ~read_flags;
}

/*
 * Check whether the flags of the current command, the permissions of the kdb
 * console and the lockdown state allow a command to be run.
 */
static bool kdb_check_flags(kdb_cmdflags_t flags, int permissions,
				   bool no_args)
{
	/* permissions comes from userspace so needs massaging slightly */
	permissions &= KDB_ENABLE_MASK;
	permissions |= KDB_ENABLE_ALWAYS_SAFE;

	/* some commands change group when launched with no arguments */
	if (no_args)
		permissions |= permissions << KDB_ENABLE_NO_ARGS_SHIFT;

	flags |= KDB_ENABLE_ALL;

	return permissions & flags;
}

/*
 * kdbgetenv - This function will return the character string value of
 *	an environment variable.
 * Parameters:
 *	match	A character string representing an environment variable.
 * Returns:
 *	NULL	No environment variable matches 'match'
 *	char*	Pointer to string value of environment variable.
 */
char *kdbgetenv(const char *match)
{
	char **ep = __env;
	int matchlen = strlen(match);
	int i;

	for (i = 0; i < __nenv; i++) {
		char *e = *ep++;

		if (!e)
			continue;

		if ((strncmp(match, e, matchlen) == 0)
		 && ((e[matchlen] == '\0')
		   || (e[matchlen] == '='))) {
			char *cp = strchr(e, '=');
			return cp ? ++cp : "";
		}
	}
	return NULL;
}

/*
 * kdballocenv - This function is used to allocate bytes for
 *	environment entries.
 * Parameters:
 *	bytes	The number of bytes to allocate in the static buffer.
 * Returns:
 *	A pointer to the allocated space in the buffer on success.
 *	NULL if bytes > size available in the envbuffer.
 * Remarks:
 *	We use a static environment buffer (envbuffer) to hold the values
 *	of dynamically generated environment variables (see kdb_set).  Buffer
 *	space once allocated is never free'd, so over time, the amount of space
 *	(currently 512 bytes) will be exhausted if env variables are changed
 *	frequently.
 */
static char *kdballocenv(size_t bytes)
{
#define	KDB_ENVBUFSIZE	512
	static char envbuffer[KDB_ENVBUFSIZE];
	static int envbufsize;
	char *ep = NULL;

	if ((KDB_ENVBUFSIZE - envbufsize) >= bytes) {
		ep = &envbuffer[envbufsize];
		envbufsize += bytes;
	}
	return ep;
}

/*
 * kdbgetulenv - This function will return the value of an unsigned
 *	long-valued environment variable.
 * Parameters:
 *	match	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long representation of the env variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 */
static int kdbgetulenv(const char *match, unsigned long *value)
{
	char *ep;

	ep = kdbgetenv(match);
	if (!ep)
		return KDB_NOTENV;
	if (strlen(ep) == 0)
		return KDB_NOENVVALUE;

	*value = simple_strtoul(ep, NULL, 0);

	return 0;
}

/*
 * kdbgetintenv - This function will return the value of an
 *	integer-valued environment variable.
 * Parameters:
 *	match	A character string representing an integer-valued env variable
 * Outputs:
 *	*value  the integer representation of the environment variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 */
int kdbgetintenv(const char *match, int *value)
{
	unsigned long val;
	int diag;

	diag = kdbgetulenv(match, &val);
	if (!diag)
		*value = (int) val;
	return diag;
}

/*
 * kdb_setenv() - Alter an existing environment variable or create a new one.
 * @var: Name of the variable
 * @val: Value of the variable
 *
 * Return: Zero on success, a kdb diagnostic on failure.
 */
static int kdb_setenv(const char *var, const char *val)
{
	int i;
	char *ep;
	size_t varlen, vallen;

	varlen = strlen(var);
	vallen = strlen(val);
	ep = kdballocenv(varlen + vallen + 2);
	if (ep == (char *)0)
		return KDB_ENVBUFFULL;

	sprintf(ep, "%s=%s", var, val);

	for (i = 0; i < __nenv; i++) {
		if (__env[i]
		 && ((strncmp(__env[i], var, varlen) == 0)
		   && ((__env[i][varlen] == '\0')
		    || (__env[i][varlen] == '=')))) {
			__env[i] = ep;
			return 0;
		}
	}

	/*
	 * Wasn't existing variable.  Fit into slot.
	 */
	for (i = 0; i < __nenv-1; i++) {
		if (__env[i] == (char *)0) {
			__env[i] = ep;
			return 0;
		}
	}

	return KDB_ENVFULL;
}

/*
 * kdb_printenv() - Display the current environment variables.
 */
static void kdb_printenv(void)
{
	int i;

	for (i = 0; i < __nenv; i++) {
		if (__env[i])
			kdb_printf("%s\n", __env[i]);
	}
}

/*
 * kdbgetularg - This function will convert a numeric string into an
 *	unsigned long value.
 * Parameters:
 *	arg	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long representation of arg.
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
 */
int kdbgetularg(const char *arg, unsigned long *value)
{
	char *endp;
	unsigned long val;

	val = simple_strtoul(arg, &endp, 0);

	if (endp == arg) {
		/*
		 * Also try base 16, for us folks too lazy to type the
		 * leading 0x...
		 */
		val = simple_strtoul(arg, &endp, 16);
		if (endp == arg)
			return KDB_BADINT;
	}

	*value = val;

	return 0;
}

int kdbgetu64arg(const char *arg, u64 *value)
{
	char *endp;
	u64 val;

	val = simple_strtoull(arg, &endp, 0);

	if (endp == arg) {

		val = simple_strtoull(arg, &endp, 16);
		if (endp == arg)
			return KDB_BADINT;
	}

	*value = val;

	return 0;
}

/*
 * kdb_set - This function implements the 'set' command.  Alter an
 *	existing environment variable or create a new one.
 */
int kdb_set(int argc, const char **argv)
{
	/*
	 * we can be invoked two ways:
	 *   set var=value    argv[1]="var", argv[2]="value"
	 *   set var = value  argv[1]="var", argv[2]="=", argv[3]="value"
	 * - if the latter, shift 'em down.
	 */
	if (argc == 3) {
		argv[2] = argv[3];
		argc--;
	}

	if (argc != 2)
		return KDB_ARGCOUNT;

	/*
	 * Censor sensitive variables
	 */
	if (strcmp(argv[1], "PROMPT") == 0 &&
	    !kdb_check_flags(KDB_ENABLE_MEM_READ, kdb_cmd_enabled, false))
		return KDB_NOPERM;

	/*
	 * Check for internal variables
	 */
	if (strcmp(argv[1], "KDBDEBUG") == 0) {
		unsigned int debugflags;
		char *cp;

		debugflags = simple_strtoul(argv[2], &cp, 0);
		if (cp == argv[2] || debugflags & ~KDB_DEBUG_FLAG_MASK) {
			kdb_printf("kdb: illegal debug flags '%s'\n",
				    argv[2]);
			return 0;
		}
		kdb_flags = (kdb_flags & ~KDB_DEBUG(MASK))
			| (debugflags << KDB_DEBUG_FLAG_SHIFT);

		return 0;
	}

	/*
	 * Tokenizer squashed the '=' sign.  argv[1] is variable
	 * name, argv[2] = value.
	 */
	return kdb_setenv(argv[1], argv[2]);
}

static int kdb_check_regs(void)
{
	if (!kdb_current_regs) {
		kdb_printf("No current kdb registers."
			   "  You may need to select another task\n");
		return KDB_BADREG;
	}
	return 0;
}

/*
 * kdbgetaddrarg - This function is responsible for parsing an
 *	address-expression and returning the value of the expression,
 *	symbol name, and offset to the caller.
 *
 *	The argument may consist of a numeric value (decimal or
 *	hexadecimal), a symbol name, a register name (preceded by the
 *	percent sign), an environment variable with a numeric value
 *	(preceded by a dollar sign) or a simple arithmetic expression
 *	consisting of a symbol name, +/-, and a numeric constant value
 *	(offset).
 * Parameters:
 *	argc	- count of arguments in argv
 *	argv	- argument vector
 *	*nextarg - index to next unparsed argument in argv[]
 *	regs	- Register state at time of KDB entry
 * Outputs:
 *	*value	- receives the value of the address-expression
 *	*offset - receives the offset specified, if any
 *	*name   - receives the symbol name, if any
 *	*nextarg - index to next unparsed argument in argv[]
 * Returns:
 *	zero is returned on success, a kdb diagnostic code is
 *      returned on error.
 */
int kdbgetaddrarg(int argc, const char **argv, int *nextarg,
		  unsigned long *value,  long *offset,
		  char **name)
{
	unsigned long addr;
	unsigned long off = 0;
	int positive;
	int diag;
	int found = 0;
	char *symname;
	char symbol = '\0';
	char *cp;
	kdb_symtab_t symtab;

	/*
	 * If the enable flags prohibit both arbitrary memory access
	 * and flow control then there are no reasonable grounds to
	 * provide symbol lookup.
	 */
	if (!kdb_check_flags(KDB_ENABLE_MEM_READ | KDB_ENABLE_FLOW_CTRL,
			     kdb_cmd_enabled, false))
		return KDB_NOPERM;

	/*
	 * Process arguments which follow the following syntax:
	 *
	 *  symbol | numeric-address [+/- numeric-offset]
	 *  %register
	 *  $environment-variable
	 */

	if (*nextarg > argc)
		return KDB_ARGCOUNT;

	symname = (char *)argv[*nextarg];

	/*
	 * If there is no whitespace between the symbol
	 * or address and the '+' or '-' symbols, we
	 * remember the character and replace it with a
	 * null so the symbol/value can be properly parsed
	 */
	cp = strpbrk(symname, "+-");
	if (cp != NULL) {
		symbol = *cp;
		*cp++ = '\0';
	}

	if (symname[0] == '$') {
		diag = kdbgetulenv(&symname[1], &addr);
		if (diag)
			return diag;
	} else if (symname[0] == '%') {
		diag = kdb_check_regs();
		if (diag)
			return diag;
		/* Implement register values with % at a later time as it is
		 * arch optional.
		 */
		return KDB_NOTIMP;
	} else {
		found = kdbgetsymval(symname, &symtab);
		if (found) {
			addr = symtab.sym_start;
		} else {
			diag = kdbgetularg(argv[*nextarg], &addr);
			if (diag)
				return diag;
		}
	}

	if (!found)
		found = kdbnearsym(addr, &symtab);

	(*nextarg)++;

	if (name)
		*name = symname;
	if (value)
		*value = addr;
	if (offset && name && *name)
		*offset = addr - symtab.sym_start;

	if ((*nextarg > argc)
	 && (symbol == '\0'))
		return 0;

	/*
	 * check for +/- and offset
	 */

	if (symbol == '\0') {
		if ((argv[*nextarg][0] != '+')
		 && (argv[*nextarg][0] != '-')) {
			/*
			 * Not our argument.  Return.
			 */
			return 0;
		} else {
			positive = (argv[*nextarg][0] == '+');
			(*nextarg)++;
		}
	} else
		positive = (symbol == '+');

	/*
	 * Now there must be an offset!
	 */
	if ((*nextarg > argc)
	 && (symbol == '\0')) {
		return KDB_INVADDRFMT;
	}

	if (!symbol) {
		cp = (char *)argv[*nextarg];
		(*nextarg)++;
	}

	diag = kdbgetularg(cp, &off);
	if (diag)
		return diag;

	if (!positive)
		off = -off;

	if (offset)
		*offset += off;

	if (value)
		*value += off;

	return 0;
}

static void kdb_cmderror(int diag)
{
	int i;

	if (diag >= 0) {
		kdb_printf("no error detected (diagnostic is %d)\n", diag);
		return;
	}

	for (i = 0; i < __nkdb_err; i++) {
		if (kdbmsgs[i].km_diag == diag) {
			kdb_printf("diag: %d: %s\n", diag, kdbmsgs[i].km_msg);
			return;
		}
	}

	kdb_printf("Unknown diag %d\n", -diag);
}

/*
 * kdb_defcmd, kdb_defcmd2 - This function implements the 'defcmd'
 *	command which defines one command as a set of other commands,
 *	terminated by endefcmd.  kdb_defcmd processes the initial
 *	'defcmd' command, kdb_defcmd2 is invoked from kdb_parse for
 *	the following commands until 'endefcmd'.
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Returns:
 *	zero for success, a kdb diagnostic if error
 */
struct kdb_macro {
	kdbtab_t cmd;			/* Macro command */
	struct list_head statements;	/* Associated statement list */
};

struct kdb_macro_statement {
	char *statement;		/* Statement text */
	struct list_head list_node;	/* Statement list node */
};

static struct kdb_macro *kdb_macro;
static bool defcmd_in_progress;

/* Forward references */
static int kdb_exec_defcmd(int argc, const char **argv);

static int kdb_defcmd2(const char *cmdstr, const char *argv0)
{
	struct kdb_macro_statement *kms;

	if (!kdb_macro)
		return KDB_NOTIMP;

	if (strcmp(argv0, "endefcmd") == 0) {
		defcmd_in_progress = false;
		if (!list_empty(&kdb_macro->statements))
			kdb_register(&kdb_macro->cmd);
		return 0;
	}

	kms = kmalloc(sizeof(*kms), GFP_KDB);
	if (!kms) {
		kdb_printf("Could not allocate new kdb macro command: %s\n",
			   cmdstr);
		return KDB_NOTIMP;
	}

	kms->statement = kdb_strdup(cmdstr, GFP_KDB);
	list_add_tail(&kms->list_node, &kdb_macro->statements);

	return 0;
}

static int kdb_defcmd(int argc, const char **argv)
{
	kdbtab_t *mp;

	if (defcmd_in_progress) {
		kdb_printf("kdb: nested defcmd detected, assuming missing "
			   "endefcmd\n");
		kdb_defcmd2("endefcmd", "endefcmd");
	}
	if (argc == 0) {
		kdbtab_t *kp;
		struct kdb_macro *kmp;
		struct kdb_macro_statement *kms;

		list_for_each_entry(kp, &kdb_cmds_head, list_node) {
			if (kp->func == kdb_exec_defcmd) {
				kdb_printf("defcmd %s \"%s\" \"%s\"\n",
					   kp->name, kp->usage, kp->help);
				kmp = container_of(kp, struct kdb_macro, cmd);
				list_for_each_entry(kms, &kmp->statements,
						    list_node)
					kdb_printf("%s", kms->statement);
				kdb_printf("endefcmd\n");
			}
		}
		return 0;
	}
	if (argc != 3)
		return KDB_ARGCOUNT;
	if (in_dbg_master()) {
		kdb_printf("Command only available during kdb_init()\n");
		return KDB_NOTIMP;
	}
	kdb_macro = kzalloc(sizeof(*kdb_macro), GFP_KDB);
	if (!kdb_macro)
		goto fail_defcmd;

	mp = &kdb_macro->cmd;
	mp->func = kdb_exec_defcmd;
	mp->minlen = 0;
	mp->flags = KDB_ENABLE_ALWAYS_SAFE;
	mp->name = kdb_strdup(argv[1], GFP_KDB);
	if (!mp->name)
		goto fail_name;
	mp->usage = kdb_strdup(argv[2], GFP_KDB);
	if (!mp->usage)
		goto fail_usage;
	mp->help = kdb_strdup(argv[3], GFP_KDB);
	if (!mp->help)
		goto fail_help;
	if (mp->usage[0] == '"') {
		strcpy(mp->usage, argv[2]+1);
		mp->usage[strlen(mp->usage)-1] = '\0';
	}
	if (mp->help[0] == '"') {
		strcpy(mp->help, argv[3]+1);
		mp->help[strlen(mp->help)-1] = '\0';
	}

	INIT_LIST_HEAD(&kdb_macro->statements);
	defcmd_in_progress = true;
	return 0;
fail_help:
	kfree(mp->usage);
fail_usage:
	kfree(mp->name);
fail_name:
	kfree(kdb_macro);
fail_defcmd:
	kdb_printf("Could not allocate new kdb_macro entry for %s\n", argv[1]);
	return KDB_NOTIMP;
}

/*
 * kdb_exec_defcmd - Execute the set of commands associated with this
 *	defcmd name.
 * Inputs:
 *	argc	argument count
 *	argv	argument vector
 * Returns:
 *	zero for success, a kdb diagnostic if error
 */
static int kdb_exec_defcmd(int argc, const char **argv)
{
	int ret;
	kdbtab_t *kp;
	struct kdb_macro *kmp;
	struct kdb_macro_statement *kms;

	if (argc != 0)
		return KDB_ARGCOUNT;

	list_for_each_entry(kp, &kdb_cmds_head, list_node) {
		if (strcmp(kp->name, argv[0]) == 0)
			break;
	}
	if (list_entry_is_head(kp, &kdb_cmds_head, list_node)) {
		kdb_printf("kdb_exec_defcmd: could not find commands for %s\n",
			   argv[0]);
		return KDB_NOTIMP;
	}
	kmp = container_of(kp, struct kdb_macro, cmd);
	list_for_each_entry(kms, &kmp->statements, list_node) {
		/*
		 * Recursive use of kdb_parse, do not use argv after this point.
		 */
		argv = NULL;
		kdb_printf("[%s]kdb> %s\n", kmp->cmd.name, kms->statement);
		ret = kdb_parse(kms->statement);
		if (ret)
			return ret;
	}
	return 0;
}

/* Command history */
#define KDB_CMD_HISTORY_COUNT	32
#define CMD_BUFLEN		200	/* kdb_printf: max printline
					 * size == 256 */
static unsigned int cmd_head, cmd_tail;
static unsigned int cmdptr;
static char cmd_hist[KDB_CMD_HISTORY_COUNT][CMD_BUFLEN];
static char cmd_cur[CMD_BUFLEN];

/*
 * The "str" argument may point to something like  | grep xyz
 */
static void parse_grep(const char *str)
{
	int	len;
	char	*cp = (char *)str, *cp2;

	/* sanity check: we should have been called with the \ first */
	if (*cp != '|')
		return;
	cp++;
	while (isspace(*cp))
		cp++;
	if (!str_has_prefix(cp, "grep ")) {
		kdb_printf("invalid 'pipe', see grephelp\n");
		return;
	}
	cp += 5;
	while (isspace(*cp))
		cp++;
	cp2 = strchr(cp, '\n');
	if (cp2)
		*cp2 = '\0'; /* remove the trailing newline */
	len = strlen(cp);
	if (len == 0) {
		kdb_printf("invalid 'pipe', see grephelp\n");
		return;
	}
	/* now cp points to a nonzero length search string */
	if (*cp == '"') {
		/* allow it be "x y z" by removing the "'s - there must
		   be two of them */
		cp++;
		cp2 = strchr(cp, '"');
		if (!cp2) {
			kdb_printf("invalid quoted string, see grephelp\n");
			return;
		}
		*cp2 = '\0'; /* end the string where the 2nd " was */
	}
	kdb_grep_leading = 0;
	if (*cp == '^') {
		kdb_grep_leading = 1;
		cp++;
	}
	len = strlen(cp);
	kdb_grep_trailing = 0;
	if (*(cp+len-1) == '$') {
		kdb_grep_trailing = 1;
		*(cp+len-1) = '\0';
	}
	len = strlen(cp);
	if (!len)
		return;
	if (len >= KDB_GREP_STRLEN) {
		kdb_printf("search string too long\n");
		return;
	}
	strcpy(kdb_grep_string, cp);
	kdb_grepping_flag++;
	return;
}

/*
 * kdb_parse - Parse the command line, search the command table for a
 *	matching command and invoke the command function.  This
 *	function may be called recursively, if it is, the second call
 *	will overwrite argv and cbuf.  It is the caller's
 *	responsibility to save their argv if they recursively call
 *	kdb_parse().
 * Parameters:
 *      cmdstr	The input command line to be parsed.
 *	regs	The registers at the time kdb was entered.
 * Returns:
 *	Zero for success, a kdb diagnostic if failure.
 * Remarks:
 *	Limited to 20 tokens.
 *
 *	Real rudimentary tokenization. Basically only whitespace
 *	is considered a token delimiter (but special consideration
 *	is taken of the '=' sign as used by the 'set' command).
 *
 *	The algorithm used to tokenize the input string relies on
 *	there being at least one whitespace (or otherwise useless)
 *	character between tokens as the character immediately following
 *	the token is altered in-place to a null-byte to terminate the
 *	token string.
 */

#define MAXARGC	20

int kdb_parse(const char *cmdstr)
{
	static char *argv[MAXARGC];
	static int argc;
	static char cbuf[CMD_BUFLEN+2];
	char *cp;
	char *cpp, quoted;
	kdbtab_t *tp;
	int escaped, ignore_errors = 0, check_grep = 0;

	/*
	 * First tokenize the command string.
	 */
	cp = (char *)cmdstr;

	if (KDB_FLAG(CMD_INTERRUPT)) {
		/* Previous command was interrupted, newline must not
		 * repeat the command */
		KDB_FLAG_CLEAR(CMD_INTERRUPT);
		KDB_STATE_SET(PAGER);
		argc = 0;	/* no repeat */
	}

	if (*cp != '\n' && *cp != '\0') {
		argc = 0;
		cpp = cbuf;
		while (*cp) {
			/* skip whitespace */
			while (isspace(*cp))
				cp++;
			if ((*cp == '\0') || (*cp == '\n') ||
			    (*cp == '#' && !defcmd_in_progress))
				break;
			/* special case: check for | grep pattern */
			if (*cp == '|') {
				check_grep++;
				break;
			}
			if (cpp >= cbuf + CMD_BUFLEN) {
				kdb_printf("kdb_parse: command buffer "
					   "overflow, command ignored\n%s\n",
					   cmdstr);
				return KDB_NOTFOUND;
			}
			if (argc >= MAXARGC - 1) {
				kdb_printf("kdb_parse: too many arguments, "
					   "command ignored\n%s\n", cmdstr);
				return KDB_NOTFOUND;
			}
			argv[argc++] = cpp;
			escaped = 0;
			quoted = '\0';
			/* Copy to next unquoted and unescaped
			 * whitespace or '=' */
			while (*cp && *cp != '\n' &&
			       (escaped || quoted || !isspace(*cp))) {
				if (cpp >= cbuf + CMD_BUFLEN)
					break;
				if (escaped) {
					escaped = 0;
					*cpp++ = *cp++;
					continue;
				}
				if (*cp == '\\') {
					escaped = 1;
					++cp;
					continue;
				}
				if (*cp == quoted)
					quoted = '\0';
				else if (*cp == '\'' || *cp == '"')
					quoted = *cp;
				*cpp = *cp++;
				if (*cpp == '=' && !quoted)
					break;
				++cpp;
			}
			*cpp++ = '\0';	/* Squash a ws or '=' character */
		}
	}
	if (!argc)
		return 0;
	if (check_grep)
		parse_grep(cp);
	if (defcmd_in_progress) {
		int result = kdb_defcmd2(cmdstr, argv[0]);
		if (!defcmd_in_progress) {
			argc = 0;	/* avoid repeat on endefcmd */
			*(argv[0]) = '\0';
		}
		return result;
	}
	if (argv[0][0] == '-' && argv[0][1] &&
	    (argv[0][1] < '0' || argv[0][1] > '9')) {
		ignore_errors = 1;
		++argv[0];
	}

	list_for_each_entry(tp, &kdb_cmds_head, list_node) {
		/*
		 * If this command is allowed to be abbreviated,
		 * check to see if this is it.
		 */
		if (tp->minlen && (strlen(argv[0]) <= tp->minlen) &&
		    (strncmp(argv[0], tp->name, tp->minlen) == 0))
			break;

		if (strcmp(argv[0], tp->name) == 0)
			break;
	}

	/*
	 * If we don't find a command by this name, see if the first
	 * few characters of this match any of the known commands.
	 * e.g., md1c20 should match md.
	 */
	if (list_entry_is_head(tp, &kdb_cmds_head, list_node)) {
		list_for_each_entry(tp, &kdb_cmds_head, list_node) {
			if (strncmp(argv[0], tp->name, strlen(tp->name)) == 0)
				break;
		}
	}

	if (!list_entry_is_head(tp, &kdb_cmds_head, list_node)) {
		int result;

		if (!kdb_check_flags(tp->flags, kdb_cmd_enabled, argc <= 1))
			return KDB_NOPERM;

		KDB_STATE_SET(CMD);
		result = (*tp->func)(argc-1, (const char **)argv);
		if (result && ignore_errors && result > KDB_CMD_GO)
			result = 0;
		KDB_STATE_CLEAR(CMD);

		if (tp->flags & KDB_REPEAT_WITH_ARGS)
			return result;

		argc = tp->flags & KDB_REPEAT_NO_ARGS ? 1 : 0;
		if (argv[argc])
			*(argv[argc]) = '\0';
		return result;
	}

	/*
	 * If the input with which we were presented does not
	 * map to an existing command, attempt to parse it as an
	 * address argument and display the result.   Useful for
	 * obtaining the address of a variable, or the nearest symbol
	 * to an address contained in a register.
	 */
	{
		unsigned long value;
		char *name = NULL;
		long offset;
		int nextarg = 0;

		if (kdbgetaddrarg(0, (const char **)argv, &nextarg,
				  &value, &offset, &name)) {
			return KDB_NOTFOUND;
		}

		kdb_printf("%s = ", argv[0]);
		kdb_symbol_print(value, NULL, KDB_SP_DEFAULT);
		kdb_printf("\n");
		return 0;
	}
}


static int handle_ctrl_cmd(char *cmd)
{
#define CTRL_P	16
#define CTRL_N	14

	/* initial situation */
	if (cmd_head == cmd_tail)
		return 0;
	switch (*cmd) {
	case CTRL_P:
		if (cmdptr != cmd_tail)
			cmdptr = (cmdptr + KDB_CMD_HISTORY_COUNT - 1) %
				 KDB_CMD_HISTORY_COUNT;
		strscpy(cmd_cur, cmd_hist[cmdptr], CMD_BUFLEN);
		return 1;
	case CTRL_N:
		if (cmdptr != cmd_head)
			cmdptr = (cmdptr+1) % KDB_CMD_HISTORY_COUNT;
		strscpy(cmd_cur, cmd_hist[cmdptr], CMD_BUFLEN);
		return 1;
	}
	return 0;
}

/*
 * kdb_reboot - This function implements the 'reboot' command.  Reboot
 *	the system immediately, or loop for ever on failure.
 */
static int kdb_reboot(int argc, const char **argv)
{
	emergency_restart();
	kdb_printf("Hmm, kdb_reboot did not reboot, spinning here\n");
	while (1)
		cpu_relax();
	/* NOTREACHED */
	return 0;
}

static void kdb_dumpregs(struct pt_regs *regs)
{
	int old_lvl = console_loglevel;
	console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
	kdb_trap_printk++;
	show_regs(regs);
	kdb_trap_printk--;
	kdb_printf("\n");
	console_loglevel = old_lvl;
}

static void kdb_set_current_task(struct task_struct *p)
{
	kdb_current_task = p;

	if (kdb_task_has_cpu(p)) {
		kdb_current_regs = KDB_TSKREGS(kdb_process_cpu(p));
		return;
	}
	kdb_current_regs = NULL;
}

static void drop_newline(char *buf)
{
	size_t len = strlen(buf);

	if (len == 0)
		return;
	if (*(buf + len - 1) == '\n')
		*(buf + len - 1) = '\0';
}

/*
 * kdb_local - The main code for kdb.  This routine is invoked on a
 *	specific processor, it is not global.  The main kdb() routine
 *	ensures that only one processor at a time is in this routine.
 *	This code is called with the real reason code on the first
 *	entry to a kdb session, thereafter it is called with reason
 *	SWITCH, even if the user goes back to the original cpu.
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	regs		The exception frame at time of fault/breakpoint.
 *	db_result	Result code from the break or debug point.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 *	KDB_CMD_GO	User typed 'go'.
 *	KDB_CMD_CPU	User switched to another cpu.
 *	KDB_CMD_SS	Single step.
 */
static int kdb_local(kdb_reason_t reason, int error, struct pt_regs *regs,
		     kdb_dbtrap_t db_result)
{
	char *cmdbuf;
	int diag;
	struct task_struct *kdb_current =
		kdb_curr_task(raw_smp_processor_id());

	KDB_DEBUG_STATE("kdb_local 1", reason);

	kdb_check_for_lockdown();

	kdb_go_count = 0;
	if (reason == KDB_REASON_DEBUG) {
		/* special case below */
	} else {
		kdb_printf("\nEntering kdb (current=0x%px, pid %d) ",
			   kdb_current, kdb_current ? kdb_current->pid : 0);
#if defined(CONFIG_SMP)
		kdb_printf("on processor %d ", raw_smp_processor_id());
#endif
	}

	switch (reason) {
	case KDB_REASON_DEBUG:
	{
		/*
		 * If re-entering kdb after a single step
		 * command, don't print the message.
		 */
		switch (db_result) {
		case KDB_DB_BPT:
			kdb_printf("\nEntering kdb (0x%px, pid %d) ",
				   kdb_current, kdb_current->pid);
#if defined(CONFIG_SMP)
			kdb_printf("on processor %d ", raw_smp_processor_id());
#endif
			kdb_printf("due to Debug @ " kdb_machreg_fmt "\n",
				   instruction_pointer(regs));
			break;
		case KDB_DB_SS:
			break;
		case KDB_DB_SSBPT:
			KDB_DEBUG_STATE("kdb_local 4", reason);
			return 1;	/* kdba_db_trap did the work */
		default:
			kdb_printf("kdb: Bad result from kdba_db_trap: %d\n",
				   db_result);
			break;
		}

	}
		break;
	case KDB_REASON_ENTER:
		if (KDB_STATE(KEYBOARD))
			kdb_printf("due to Keyboard Entry\n");
		else
			kdb_printf("due to KDB_ENTER()\n");
		break;
	case KDB_REASON_KEYBOARD:
		KDB_STATE_SET(KEYBOARD);
		kdb_printf("due to Keyboard Entry\n");
		break;
	case KDB_REASON_ENTER_SLAVE:
		/* drop through, slaves only get released via cpu switch */
	case KDB_REASON_SWITCH:
		kdb_printf("due to cpu switch\n");
		break;
	case KDB_REASON_OOPS:
		kdb_printf("Oops: %s\n", kdb_diemsg);
		kdb_printf("due to oops @ " kdb_machreg_fmt "\n",
			   instruction_pointer(regs));
		kdb_dumpregs(regs);
		break;
	case KDB_REASON_SYSTEM_NMI:
		kdb_printf("due to System NonMaskable Interrupt\n");
		break;
	case KDB_REASON_NMI:
		kdb_printf("due to NonMaskable Interrupt @ "
			   kdb_machreg_fmt "\n",
			   instruction_pointer(regs));
		break;
	case KDB_REASON_SSTEP:
	case KDB_REASON_BREAK:
		kdb_printf("due to %s @ " kdb_machreg_fmt "\n",
			   reason == KDB_REASON_BREAK ?
			   "Breakpoint" : "SS trap", instruction_pointer(regs));
		/*
		 * Determine if this breakpoint is one that we
		 * are interested in.
		 */
		if (db_result != KDB_DB_BPT) {
			kdb_printf("kdb: error return from kdba_bp_trap: %d\n",
				   db_result);
			KDB_DEBUG_STATE("kdb_local 6", reason);
			return 0;	/* Not for us, dismiss it */
		}
		break;
	case KDB_REASON_RECURSE:
		kdb_printf("due to Recursion @ " kdb_machreg_fmt "\n",
			   instruction_pointer(regs));
		break;
	default:
		kdb_printf("kdb: unexpected reason code: %d\n", reason);
		KDB_DEBUG_STATE("kdb_local 8", reason);
		return 0;	/* Not for us, dismiss it */
	}

	while (1) {
		/*
		 * Initialize pager context.
		 */
		kdb_nextline = 1;
		KDB_STATE_CLEAR(SUPPRESS);
		kdb_grepping_flag = 0;
		/* ensure the old search does not leak into '/' commands */
		kdb_grep_string[0] = '\0';

		cmdbuf = cmd_cur;
		*cmdbuf = '\0';
		*(cmd_hist[cmd_head]) = '\0';

do_full_getstr:
		/* PROMPT can only be set if we have MEM_READ permission. */
		snprintf(kdb_prompt_str, CMD_BUFLEN, kdbgetenv("PROMPT"),
			 raw_smp_processor_id());
		if (defcmd_in_progress)
			strncat(kdb_prompt_str, "[defcmd]", CMD_BUFLEN);

		/*
		 * Fetch command from keyboard
		 */
		cmdbuf = kdb_getstr(cmdbuf, CMD_BUFLEN, kdb_prompt_str);
		if (*cmdbuf != '\n') {
			if (*cmdbuf < 32) {
				if (cmdptr == cmd_head) {
					strscpy(cmd_hist[cmd_head], cmd_cur,
						CMD_BUFLEN);
					*(cmd_hist[cmd_head] +
					  strlen(cmd_hist[cmd_head])-1) = '\0';
				}
				if (!handle_ctrl_cmd(cmdbuf))
					*(cmd_cur+strlen(cmd_cur)-1) = '\0';
				cmdbuf = cmd_cur;
				goto do_full_getstr;
			} else {
				strscpy(cmd_hist[cmd_head], cmd_cur,
					CMD_BUFLEN);
			}

			cmd_head = (cmd_head+1) % KDB_CMD_HISTORY_COUNT;
			if (cmd_head == cmd_tail)
				cmd_tail = (cmd_tail+1) % KDB_CMD_HISTORY_COUNT;
		}

		cmdptr = cmd_head;
		diag = kdb_parse(cmdbuf);
		if (diag == KDB_NOTFOUND) {
			drop_newline(cmdbuf);
			kdb_printf("Unknown kdb command: '%s'\n", cmdbuf);
			diag = 0;
		}
		if (diag == KDB_CMD_GO
		 || diag == KDB_CMD_CPU
		 || diag == KDB_CMD_SS
		 || diag == KDB_CMD_KGDB)
			break;

		if (diag)
			kdb_cmderror(diag);
	}
	KDB_DEBUG_STATE("kdb_local 9", diag);
	return diag;
}


/*
 * kdb_print_state - Print the state data for the current processor
 *	for debugging.
 * Inputs:
 *	text		Identifies the debug point
 *	value		Any integer value to be printed, e.g. reason code.
 */
void kdb_print_state(const char *text, int value)
{
	kdb_printf("state: %s cpu %d value %d initial %d state %x\n",
		   text, raw_smp_processor_id(), value, kdb_initial_cpu,
		   kdb_state);
}

/*
 * kdb_main_loop - After initial setup and assignment of the
 *	controlling cpu, all cpus are in this loop.  One cpu is in
 *	control and will issue the kdb prompt, the others will spin
 *	until 'go' or cpu switch.
 *
 *	To get a consistent view of the kernel stacks for all
 *	processes, this routine is invoked from the main kdb code via
 *	an architecture specific routine.  kdba_main_loop is
 *	responsible for making the kernel stacks consistent for all
 *	processes, there should be no difference between a blocked
 *	process and a running process as far as kdb is concerned.
 * Inputs:
 *	reason		The reason KDB was invoked
 *	error		The hardware-defined error code
 *	reason2		kdb's current reason code.
 *			Initially error but can change
 *			according to kdb state.
 *	db_result	Result code from break or debug point.
 *	regs		The exception frame at time of fault/breakpoint.
 *			should always be valid.
 * Returns:
 *	0	KDB was invoked for an event which it wasn't responsible
 *	1	KDB handled the event for which it was invoked.
 */
int kdb_main_loop(kdb_reason_t reason, kdb_reason_t reason2, int error,
	      kdb_dbtrap_t db_result, struct pt_regs *regs)
{
	int result = 1;
	/* Stay in kdb() until 'go', 'ss[b]' or an error */
	while (1) {
		/*
		 * All processors except the one that is in control
		 * will spin here.
		 */
		KDB_DEBUG_STATE("kdb_main_loop 1", reason);
		while (KDB_STATE(HOLD_CPU)) {
			/* state KDB is turned off by kdb_cpu to see if the
			 * other cpus are still live, each cpu in this loop
			 * turns it back on.
			 */
			if (!KDB_STATE(KDB))
				KDB_STATE_SET(KDB);
		}

		KDB_STATE_CLEAR(SUPPRESS);
		KDB_DEBUG_STATE("kdb_main_loop 2", reason);
		if (KDB_STATE(LEAVING))
			break;	/* Another cpu said 'go' */
		/* Still using kdb, this processor is in control */
		result = kdb_local(reason2, error, regs, db_result);
		KDB_DEBUG_STATE("kdb_main_loop 3", result);

		if (result == KDB_CMD_CPU)
			break;

		if (result == KDB_CMD_SS) {
			KDB_STATE_SET(DOING_SS);
			break;
		}

		if (result == KDB_CMD_KGDB) {
			if (!KDB_STATE(DOING_KGDB))
				kdb_printf("Entering please attach debugger "
					   "or use $D#44+ or $3#33\n");
			break;
		}
		if (result && result != 1 && result != KDB_CMD_GO)
			kdb_printf("\nUnexpected kdb_local return code %d\n",
				   result);
		KDB_DEBUG_STATE("kdb_main_loop 4", reason);
		break;
	}
	if (KDB_STATE(DOING_SS))
		KDB_STATE_CLEAR(SSBPT);

	/* Clean up any keyboard devices before leaving */
	kdb_kbd_cleanup_state();

	return result;
}

/*
 * kdb_mdr - This function implements the guts of the 'mdr', memory
 * read command.
 *	mdr  <addr arg>,<byte count>
 * Inputs:
 *	addr	Start address
 *	count	Number of bytes
 * Returns:
 *	Always 0.  Any errors are detected and printed by kdb_getarea.
 */
static int kdb_mdr(unsigned long addr, unsigned int count)
{
	unsigned char c;
	while (count--) {
		if (kdb_getarea(c, addr))
			return 0;
		kdb_printf("%02x", c);
		addr++;
	}
	kdb_printf("\n");
	return 0;
}

/*
 * kdb_md - This function implements the 'md', 'md1', 'md2', 'md4',
 *	'md8' 'mdr' and 'mds' commands.
 *
 *	md|mds  [<addr arg> [<line count> [<radix>]]]
 *	mdWcN	[<addr arg> [<line count> [<radix>]]]
 *		where W = is the width (1, 2, 4 or 8) and N is the count.
 *		for eg., md1c20 reads 20 bytes, 1 at a time.
 *	mdr  <addr arg>,<byte count>
 */
static void kdb_md_line(const char *fmtstr, unsigned long addr,
			int symbolic, int nosect, int bytesperword,
			int num, int repeat, int phys)
{
	/* print just one line of data */
	kdb_symtab_t symtab;
	char cbuf[32];
	char *c = cbuf;
	int i;
	int j;
	unsigned long word;

	memset(cbuf, '\0', sizeof(cbuf));
	if (phys)
		kdb_printf("phys " kdb_machreg_fmt0 " ", addr);
	else
		kdb_printf(kdb_machreg_fmt0 " ", addr);

	for (i = 0; i < num && repeat--; i++) {
		if (phys) {
			if (kdb_getphysword(&word, addr, bytesperword))
				break;
		} else if (kdb_getword(&word, addr, bytesperword))
			break;
		kdb_printf(fmtstr, word);
		if (symbolic)
			kdbnearsym(word, &symtab);
		else
			memset(&symtab, 0, sizeof(symtab));
		if (symtab.sym_name) {
			kdb_symbol_print(word, &symtab, 0);
			if (!nosect) {
				kdb_printf("\n");
				kdb_printf("                       %s %s "
					   kdb_machreg_fmt " "
					   kdb_machreg_fmt " "
					   kdb_machreg_fmt, symtab.mod_name,
					   symtab.sec_name, symtab.sec_start,
					   symtab.sym_start, symtab.sym_end);
			}
			addr += bytesperword;
		} else {
			union {
				u64 word;
				unsigned char c[8];
			} wc;
			unsigned char *cp;
#ifdef	__BIG_ENDIAN
			cp = wc.c + 8 - bytesperword;
#else
			cp = wc.c;
#endif
			wc.word = word;
#define printable_char(c) \
	({unsigned char __c = c; isascii(__c) && isprint(__c) ? __c : '.'; })
			for (j = 0; j < bytesperword; j++)
				*c++ = printable_char(*cp++);
			addr += bytesperword;
#undef printable_char
		}
	}
	kdb_printf("%*s %s\n", (int)((num-i)*(2*bytesperword + 1)+1),
		   " ", cbuf);
}

static int kdb_md(int argc, const char **argv)
{
	static unsigned long last_addr;
	static int last_radix, last_bytesperword, last_repeat;
	int radix = 16, mdcount = 8, bytesperword = KDB_WORD_SIZE, repeat;
	int nosect = 0;
	char fmtchar, fmtstr[64];
	unsigned long addr;
	unsigned long word;
	long offset = 0;
	int symbolic = 0;
	int valid = 0;
	int phys = 0;
	int raw = 0;

	kdbgetintenv("MDCOUNT", &mdcount);
	kdbgetintenv("RADIX", &radix);
	kdbgetintenv("BYTESPERWORD", &bytesperword);

	/* Assume 'md <addr>' and start with environment values */
	repeat = mdcount * 16 / bytesperword;

	if (strcmp(argv[0], "mdr") == 0) {
		if (argc == 2 || (argc == 0 && last_addr != 0))
			valid = raw = 1;
		else
			return KDB_ARGCOUNT;
	} else if (isdigit(argv[0][2])) {
		bytesperword = (int)(argv[0][2] - '0');
		if (bytesperword == 0) {
			bytesperword = last_bytesperword;
			if (bytesperword == 0)
				bytesperword = 4;
		}
		last_bytesperword = bytesperword;
		repeat = mdcount * 16 / bytesperword;
		if (!argv[0][3])
			valid = 1;
		else if (argv[0][3] == 'c' && argv[0][4]) {
			char *p;
			repeat = simple_strtoul(argv[0] + 4, &p, 10);
			mdcount = ((repeat * bytesperword) + 15) / 16;
			valid = !*p;
		}
		last_repeat = repeat;
	} else if (strcmp(argv[0], "md") == 0)
		valid = 1;
	else if (strcmp(argv[0], "mds") == 0)
		valid = 1;
	else if (strcmp(argv[0], "mdp") == 0) {
		phys = valid = 1;
	}
	if (!valid)
		return KDB_NOTFOUND;

	if (argc == 0) {
		if (last_addr == 0)
			return KDB_ARGCOUNT;
		addr = last_addr;
		radix = last_radix;
		bytesperword = last_bytesperword;
		repeat = last_repeat;
		if (raw)
			mdcount = repeat;
		else
			mdcount = ((repeat * bytesperword) + 15) / 16;
	}

	if (argc) {
		unsigned long val;
		int diag, nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg, &addr,
				     &offset, NULL);
		if (diag)
			return diag;
		if (argc > nextarg+2)
			return KDB_ARGCOUNT;

		if (argc >= nextarg) {
			diag = kdbgetularg(argv[nextarg], &val);
			if (!diag) {
				mdcount = (int) val;
				if (raw)
					repeat = mdcount;
				else
					repeat = mdcount * 16 / bytesperword;
			}
		}
		if (argc >= nextarg+1) {
			diag = kdbgetularg(argv[nextarg+1], &val);
			if (!diag)
				radix = (int) val;
		}
	}

	if (strcmp(argv[0], "mdr") == 0) {
		int ret;
		last_addr = addr;
		ret = kdb_mdr(addr, mdcount);
		last_addr += mdcount;
		last_repeat = mdcount;
		last_bytesperword = bytesperword; // to make REPEAT happy
		return ret;
	}

	switch (radix) {
	case 10:
		fmtchar = 'd';
		break;
	case 16:
		fmtchar = 'x';
		break;
	case 8:
		fmtchar = 'o';
		break;
	default:
		return KDB_BADRADIX;
	}

	last_radix = radix;

	if (bytesperword > KDB_WORD_SIZE)
		return KDB_BADWIDTH;

	switch (bytesperword) {
	case 8:
		sprintf(fmtstr, "%%16.16l%c ", fmtchar);
		break;
	case 4:
		sprintf(fmtstr, "%%8.8l%c ", fmtchar);
		break;
	case 2:
		sprintf(fmtstr, "%%4.4l%c ", fmtchar);
		break;
	case 1:
		sprintf(fmtstr, "%%2.2l%c ", fmtchar);
		break;
	default:
		return KDB_BADWIDTH;
	}

	last_repeat = repeat;
	last_bytesperword = bytesperword;

	if (strcmp(argv[0], "mds") == 0) {
		symbolic = 1;
		/* Do not save these changes as last_*, they are temporary mds
		 * overrides.
		 */
		bytesperword = KDB_WORD_SIZE;
		repeat = mdcount;
		kdbgetintenv("NOSECT", &nosect);
	}

	/* Round address down modulo BYTESPERWORD */

	addr &= ~(bytesperword-1);

	while (repeat > 0) {
		unsigned long a;
		int n, z, num = (symbolic ? 1 : (16 / bytesperword));

		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		for (a = addr, z = 0; z < repeat; a += bytesperword, ++z) {
			if (phys) {
				if (kdb_getphysword(&word, a, bytesperword)
						|| word)
					break;
			} else if (kdb_getword(&word, a, bytesperword) || word)
				break;
		}
		n = min(num, repeat);
		kdb_md_line(fmtstr, addr, symbolic, nosect, bytesperword,
			    num, repeat, phys);
		addr += bytesperword * n;
		repeat -= n;
		z = (z + num - 1) / num;
		if (z > 2) {
			int s = num * (z-2);
			kdb_printf(kdb_machreg_fmt0 "-" kdb_machreg_fmt0
				   " zero suppressed\n",
				addr, addr + bytesperword * s - 1);
			addr += bytesperword * s;
			repeat -= s;
		}
	}
	last_addr = addr;

	return 0;
}

/*
 * kdb_mm - This function implements the 'mm' command.
 *	mm address-expression new-value
 * Remarks:
 *	mm works on machine words, mmW works on bytes.
 */
static int kdb_mm(int argc, const char **argv)
{
	int diag;
	unsigned long addr;
	long offset = 0;
	unsigned long contents;
	int nextarg;
	int width;

	if (argv[0][2] && !isdigit(argv[0][2]))
		return KDB_NOTFOUND;

	if (argc < 2)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	if (nextarg > argc)
		return KDB_ARGCOUNT;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &contents, NULL, NULL);
	if (diag)
		return diag;

	if (nextarg != argc + 1)
		return KDB_ARGCOUNT;

	width = argv[0][2] ? (argv[0][2] - '0') : (KDB_WORD_SIZE);
	diag = kdb_putword(addr, contents, width);
	if (diag)
		return diag;

	kdb_printf(kdb_machreg_fmt " = " kdb_machreg_fmt "\n", addr, contents);

	return 0;
}

/*
 * kdb_go - This function implements the 'go' command.
 *	go [address-expression]
 */
static int kdb_go(int argc, const char **argv)
{
	unsigned long addr;
	int diag;
	int nextarg;
	long offset;

	if (raw_smp_processor_id() != kdb_initial_cpu) {
		kdb_printf("go must execute on the entry cpu, "
			   "please use \"cpu %d\" and then execute go\n",
			   kdb_initial_cpu);
		return KDB_BADCPUNUM;
	}
	if (argc == 1) {
		nextarg = 1;
		diag = kdbgetaddrarg(argc, argv, &nextarg,
				     &addr, &offset, NULL);
		if (diag)
			return diag;
	} else if (argc) {
		return KDB_ARGCOUNT;
	}

	diag = KDB_CMD_GO;
	if (KDB_FLAG(CATASTROPHIC)) {
		kdb_printf("Catastrophic error detected\n");
		kdb_printf("kdb_continue_catastrophic=%d, ",
			kdb_continue_catastrophic);
		if (kdb_continue_catastrophic == 0 && kdb_go_count++ == 0) {
			kdb_printf("type go a second time if you really want "
				   "to continue\n");
			return 0;
		}
		if (kdb_continue_catastrophic == 2) {
			kdb_printf("forcing reboot\n");
			kdb_reboot(0, NULL);
		}
		kdb_printf("attempting to continue\n");
	}
	return diag;
}

/*
 * kdb_rd - This function implements the 'rd' command.
 */
static int kdb_rd(int argc, const char **argv)
{
	int len = kdb_check_regs();
#if DBG_MAX_REG_NUM > 0
	int i;
	char *rname;
	int rsize;
	u64 reg64;
	u32 reg32;
	u16 reg16;
	u8 reg8;

	if (len)
		return len;

	for (i = 0; i < DBG_MAX_REG_NUM; i++) {
		rsize = dbg_reg_def[i].size * 2;
		if (rsize > 16)
			rsize = 2;
		if (len + strlen(dbg_reg_def[i].name) + 4 + rsize > 80) {
			len = 0;
			kdb_printf("\n");
		}
		if (len)
			len += kdb_printf("  ");
		switch(dbg_reg_def[i].size * 8) {
		case 8:
			rname = dbg_get_reg(i, &reg8, kdb_current_regs);
			if (!rname)
				break;
			len += kdb_printf("%s: %02x", rname, reg8);
			break;
		case 16:
			rname = dbg_get_reg(i, &reg16, kdb_current_regs);
			if (!rname)
				break;
			len += kdb_printf("%s: %04x", rname, reg16);
			break;
		case 32:
			rname = dbg_get_reg(i, &reg32, kdb_current_regs);
			if (!rname)
				break;
			len += kdb_printf("%s: %08x", rname, reg32);
			break;
		case 64:
			rname = dbg_get_reg(i, &reg64, kdb_current_regs);
			if (!rname)
				break;
			len += kdb_printf("%s: %016llx", rname, reg64);
			break;
		default:
			len += kdb_printf("%s: ??", dbg_reg_def[i].name);
		}
	}
	kdb_printf("\n");
#else
	if (len)
		return len;

	kdb_dumpregs(kdb_current_regs);
#endif
	return 0;
}

/*
 * kdb_rm - This function implements the 'rm' (register modify)  command.
 *	rm register-name new-contents
 * Remarks:
 *	Allows register modification with the same restrictions as gdb
 */
static int kdb_rm(int argc, const char **argv)
{
#if DBG_MAX_REG_NUM > 0
	int diag;
	const char *rname;
	int i;
	u64 reg64;
	u32 reg32;
	u16 reg16;
	u8 reg8;

	if (argc != 2)
		return KDB_ARGCOUNT;
	/*
	 * Allow presence or absence of leading '%' symbol.
	 */
	rname = argv[1];
	if (*rname == '%')
		rname++;

	diag = kdbgetu64arg(argv[2], &reg64);
	if (diag)
		return diag;

	diag = kdb_check_regs();
	if (diag)
		return diag;

	diag = KDB_BADREG;
	for (i = 0; i < DBG_MAX_REG_NUM; i++) {
		if (strcmp(rname, dbg_reg_def[i].name) == 0) {
			diag = 0;
			break;
		}
	}
	if (!diag) {
		switch(dbg_reg_def[i].size * 8) {
		case 8:
			reg8 = reg64;
			dbg_set_reg(i, &reg8, kdb_current_regs);
			break;
		case 16:
			reg16 = reg64;
			dbg_set_reg(i, &reg16, kdb_current_regs);
			break;
		case 32:
			reg32 = reg64;
			dbg_set_reg(i, &reg32, kdb_current_regs);
			break;
		case 64:
			dbg_set_reg(i, &reg64, kdb_current_regs);
			break;
		}
	}
	return diag;
#else
	kdb_printf("ERROR: Register set currently not implemented\n");
    return 0;
#endif
}

#if defined(CONFIG_MAGIC_SYSRQ)
/*
 * kdb_sr - This function implements the 'sr' (SYSRQ key) command
 *	which interfaces to the soi-disant MAGIC SYSRQ functionality.
 *		sr <magic-sysrq-code>
 */
static int kdb_sr(int argc, const char **argv)
{
	bool check_mask =
	    !kdb_check_flags(KDB_ENABLE_ALL, kdb_cmd_enabled, false);

	if (argc != 1)
		return KDB_ARGCOUNT;

	kdb_trap_printk++;
	__handle_sysrq(*argv[1], check_mask);
	kdb_trap_printk--;

	return 0;
}
#endif	/* CONFIG_MAGIC_SYSRQ */

/*
 * kdb_ef - This function implements the 'regs' (display exception
 *	frame) command.  This command takes an address and expects to
 *	find an exception frame at that address, formats and prints
 *	it.
 *		regs address-expression
 * Remarks:
 *	Not done yet.
 */
static int kdb_ef(int argc, const char **argv)
{
	int diag;
	unsigned long addr;
	long offset;
	int nextarg;

	if (argc != 1)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;
	show_regs((struct pt_regs *)addr);
	return 0;
}

/*
 * kdb_env - This function implements the 'env' command.  Display the
 *	current environment variables.
 */

static int kdb_env(int argc, const char **argv)
{
	kdb_printenv();

	if (KDB_DEBUG(MASK))
		kdb_printf("KDBDEBUG=0x%x\n",
			(kdb_flags & KDB_DEBUG(MASK)) >> KDB_DEBUG_FLAG_SHIFT);

	return 0;
}

#ifdef CONFIG_PRINTK
/*
 * kdb_dmesg - This function implements the 'dmesg' command to display
 *	the contents of the syslog buffer.
 *		dmesg [lines] [adjust]
 */
static int kdb_dmesg(int argc, const char **argv)
{
	int diag;
	int logging;
	int lines = 0;
	int adjust = 0;
	int n = 0;
	int skip = 0;
	struct kmsg_dump_iter iter;
	size_t len;
	char buf[201];

	if (argc > 2)
		return KDB_ARGCOUNT;
	if (argc) {
		char *cp;
		lines = simple_strtol(argv[1], &cp, 0);
		if (*cp)
			lines = 0;
		if (argc > 1) {
			adjust = simple_strtoul(argv[2], &cp, 0);
			if (*cp || adjust < 0)
				adjust = 0;
		}
	}

	/* disable LOGGING if set */
	diag = kdbgetintenv("LOGGING", &logging);
	if (!diag && logging) {
		const char *setargs[] = { "set", "LOGGING", "0" };
		kdb_set(2, setargs);
	}

	kmsg_dump_rewind(&iter);
	while (kmsg_dump_get_line(&iter, 1, NULL, 0, NULL))
		n++;

	if (lines < 0) {
		if (adjust >= n)
			kdb_printf("buffer only contains %d lines, nothing "
				   "printed\n", n);
		else if (adjust - lines >= n)
			kdb_printf("buffer only contains %d lines, last %d "
				   "lines printed\n", n, n - adjust);
		skip = adjust;
		lines = abs(lines);
	} else if (lines > 0) {
		skip = n - lines - adjust;
		lines = abs(lines);
		if (adjust >= n) {
			kdb_printf("buffer only contains %d lines, "
				   "nothing printed\n", n);
			skip = n;
		} else if (skip < 0) {
			lines += skip;
			skip = 0;
			kdb_printf("buffer only contains %d lines, first "
				   "%d lines printed\n", n, lines);
		}
	} else {
		lines = n;
	}

	if (skip >= n || skip < 0)
		return 0;

	kmsg_dump_rewind(&iter);
	while (kmsg_dump_get_line(&iter, 1, buf, sizeof(buf), &len)) {
		if (skip) {
			skip--;
			continue;
		}
		if (!lines--)
			break;
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;

		kdb_printf("%.*s\n", (int)len - 1, buf);
	}

	return 0;
}
#endif /* CONFIG_PRINTK */

/* Make sure we balance enable/disable calls, must disable first. */
static atomic_t kdb_nmi_disabled;

static int kdb_disable_nmi(int argc, const char *argv[])
{
	if (atomic_read(&kdb_nmi_disabled))
		return 0;
	atomic_set(&kdb_nmi_disabled, 1);
	arch_kgdb_ops.enable_nmi(0);
	return 0;
}

static int kdb_param_enable_nmi(const char *val, const struct kernel_param *kp)
{
	if (!atomic_add_unless(&kdb_nmi_disabled, -1, 0))
		return -EINVAL;
	arch_kgdb_ops.enable_nmi(1);
	return 0;
}

static const struct kernel_param_ops kdb_param_ops_enable_nmi = {
	.set = kdb_param_enable_nmi,
};
module_param_cb(enable_nmi, &kdb_param_ops_enable_nmi, NULL, 0600);

/*
 * kdb_cpu - This function implements the 'cpu' command.
 *	cpu	[<cpunum>]
 * Returns:
 *	KDB_CMD_CPU for success, a kdb diagnostic if error
 */
static void kdb_cpu_status(void)
{
	int i, start_cpu, first_print = 1;
	char state, prev_state = '?';

	kdb_printf("Currently on cpu %d\n", raw_smp_processor_id());
	kdb_printf("Available cpus: ");
	for (start_cpu = -1, i = 0; i < NR_CPUS; i++) {
		if (!cpu_online(i)) {
			state = 'F';	/* cpu is offline */
		} else if (!kgdb_info[i].enter_kgdb) {
			state = 'D';	/* cpu is online but unresponsive */
		} else {
			state = ' ';	/* cpu is responding to kdb */
			if (kdb_task_state_char(KDB_TSK(i)) == '-')
				state = '-';	/* idle task */
		}
		if (state != prev_state) {
			if (prev_state != '?') {
				if (!first_print)
					kdb_printf(", ");
				first_print = 0;
				kdb_printf("%d", start_cpu);
				if (start_cpu < i-1)
					kdb_printf("-%d", i-1);
				if (prev_state != ' ')
					kdb_printf("(%c)", prev_state);
			}
			prev_state = state;
			start_cpu = i;
		}
	}
	/* print the trailing cpus, ignoring them if they are all offline */
	if (prev_state != 'F') {
		if (!first_print)
			kdb_printf(", ");
		kdb_printf("%d", start_cpu);
		if (start_cpu < i-1)
			kdb_printf("-%d", i-1);
		if (prev_state != ' ')
			kdb_printf("(%c)", prev_state);
	}
	kdb_printf("\n");
}

static int kdb_cpu(int argc, const char **argv)
{
	unsigned long cpunum;
	int diag;

	if (argc == 0) {
		kdb_cpu_status();
		return 0;
	}

	if (argc != 1)
		return KDB_ARGCOUNT;

	diag = kdbgetularg(argv[1], &cpunum);
	if (diag)
		return diag;

	/*
	 * Validate cpunum
	 */
	if ((cpunum >= CONFIG_NR_CPUS) || !kgdb_info[cpunum].enter_kgdb)
		return KDB_BADCPUNUM;

	dbg_switch_cpu = cpunum;

	/*
	 * Switch to other cpu
	 */
	return KDB_CMD_CPU;
}

/* The user may not realize that ps/bta with no parameters does not print idle
 * or sleeping system daemon processes, so tell them how many were suppressed.
 */
void kdb_ps_suppressed(void)
{
	int idle = 0, daemon = 0;
	unsigned long cpu;
	const struct task_struct *p, *g;
	for_each_online_cpu(cpu) {
		p = kdb_curr_task(cpu);
		if (kdb_task_state(p, "-"))
			++idle;
	}
	for_each_process_thread(g, p) {
		if (kdb_task_state(p, "ims"))
			++daemon;
	}
	if (idle || daemon) {
		if (idle)
			kdb_printf("%d idle process%s (state -)%s\n",
				   idle, idle == 1 ? "" : "es",
				   daemon ? " and " : "");
		if (daemon)
			kdb_printf("%d sleeping system daemon (state [ims]) "
				   "process%s", daemon,
				   daemon == 1 ? "" : "es");
		kdb_printf(" suppressed,\nuse 'ps A' to see all.\n");
	}
}

void kdb_ps1(const struct task_struct *p)
{
	int cpu;
	unsigned long tmp;

	if (!p ||
	    copy_from_kernel_nofault(&tmp, (char *)p, sizeof(unsigned long)))
		return;

	cpu = kdb_process_cpu(p);
	kdb_printf("0x%px %8d %8d  %d %4d   %c  0x%px %c%s\n",
		   (void *)p, p->pid, p->parent->pid,
		   kdb_task_has_cpu(p), kdb_process_cpu(p),
		   kdb_task_state_char(p),
		   (void *)(&p->thread),
		   p == kdb_curr_task(raw_smp_processor_id()) ? '*' : ' ',
		   p->comm);
	if (kdb_task_has_cpu(p)) {
		if (!KDB_TSK(cpu)) {
			kdb_printf("  Error: no saved data for this cpu\n");
		} else {
			if (KDB_TSK(cpu) != p)
				kdb_printf("  Error: does not match running "
				   "process table (0x%px)\n", KDB_TSK(cpu));
		}
	}
}

/*
 * kdb_ps - This function implements the 'ps' command which shows a
 *	    list of the active processes.
 *
 * ps [<state_chars>]   Show processes, optionally selecting only those whose
 *                      state character is found in <state_chars>.
 */
static int kdb_ps(int argc, const char **argv)
{
	struct task_struct *g, *p;
	const char *mask;
	unsigned long cpu;

	if (argc == 0)
		kdb_ps_suppressed();
	kdb_printf("%-*s      Pid   Parent [*] cpu State %-*s Command\n",
		(int)(2*sizeof(void *))+2, "Task Addr",
		(int)(2*sizeof(void *))+2, "Thread");
	mask = argc ? argv[1] : kdbgetenv("PS");
	/* Run the active tasks first */
	for_each_online_cpu(cpu) {
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		p = kdb_curr_task(cpu);
		if (kdb_task_state(p, mask))
			kdb_ps1(p);
	}
	kdb_printf("\n");
	/* Now the real tasks */
	for_each_process_thread(g, p) {
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		if (kdb_task_state(p, mask))
			kdb_ps1(p);
	}

	return 0;
}

/*
 * kdb_pid - This function implements the 'pid' command which switches
 *	the currently active process.
 *		pid [<pid> | R]
 */
static int kdb_pid(int argc, const char **argv)
{
	struct task_struct *p;
	unsigned long val;
	int diag;

	if (argc > 1)
		return KDB_ARGCOUNT;

	if (argc) {
		if (strcmp(argv[1], "R") == 0) {
			p = KDB_TSK(kdb_initial_cpu);
		} else {
			diag = kdbgetularg(argv[1], &val);
			if (diag)
				return KDB_BADINT;

			p = find_task_by_pid_ns((pid_t)val,	&init_pid_ns);
			if (!p) {
				kdb_printf("No task with pid=%d\n", (pid_t)val);
				return 0;
			}
		}
		kdb_set_current_task(p);
	}
	kdb_printf("KDB current process is %s(pid=%d)\n",
		   kdb_current_task->comm,
		   kdb_current_task->pid);

	return 0;
}

static int kdb_kgdb(int argc, const char **argv)
{
	return KDB_CMD_KGDB;
}

/*
 * kdb_help - This function implements the 'help' and '?' commands.
 */
static int kdb_help(int argc, const char **argv)
{
	kdbtab_t *kt;

	kdb_printf("%-15.15s %-20.20s %s\n", "Command", "Usage", "Description");
	kdb_printf("-----------------------------"
		   "-----------------------------\n");
	list_for_each_entry(kt, &kdb_cmds_head, list_node) {
		char *space = "";
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		if (!kdb_check_flags(kt->flags, kdb_cmd_enabled, true))
			continue;
		if (strlen(kt->usage) > 20)
			space = "\n                                    ";
		kdb_printf("%-15.15s %-20s%s%s\n", kt->name,
			   kt->usage, space, kt->help);
	}
	return 0;
}

/*
 * kdb_kill - This function implements the 'kill' commands.
 */
static int kdb_kill(int argc, const char **argv)
{
	long sig, pid;
	char *endp;
	struct task_struct *p;

	if (argc != 2)
		return KDB_ARGCOUNT;

	sig = simple_strtol(argv[1], &endp, 0);
	if (*endp)
		return KDB_BADINT;
	if ((sig >= 0) || !valid_signal(-sig)) {
		kdb_printf("Invalid signal parameter.<-signal>\n");
		return 0;
	}
	sig = -sig;

	pid = simple_strtol(argv[2], &endp, 0);
	if (*endp)
		return KDB_BADINT;
	if (pid <= 0) {
		kdb_printf("Process ID must be large than 0.\n");
		return 0;
	}

	/* Find the process. */
	p = find_task_by_pid_ns(pid, &init_pid_ns);
	if (!p) {
		kdb_printf("The specified process isn't found.\n");
		return 0;
	}
	p = p->group_leader;
	kdb_send_sig(p, sig);
	return 0;
}

/*
 * Most of this code has been lifted from kernel/timer.c::sys_sysinfo().
 * I cannot call that code directly from kdb, it has an unconditional
 * cli()/sti() and calls routines that take locks which can stop the debugger.
 */
static void kdb_sysinfo(struct sysinfo *val)
{
	u64 uptime = ktime_get_mono_fast_ns();

	memset(val, 0, sizeof(*val));
	val->uptime = div_u64(uptime, NSEC_PER_SEC);
	val->loads[0] = avenrun[0];
	val->loads[1] = avenrun[1];
	val->loads[2] = avenrun[2];
	val->procs = nr_threads-1;
	si_meminfo(val);

	return;
}

/*
 * kdb_summary - This function implements the 'summary' command.
 */
static int kdb_summary(int argc, const char **argv)
{
	time64_t now;
	struct sysinfo val;

	if (argc)
		return KDB_ARGCOUNT;

	kdb_printf("sysname    %s\n", init_uts_ns.name.sysname);
	kdb_printf("release    %s\n", init_uts_ns.name.release);
	kdb_printf("version    %s\n", init_uts_ns.name.version);
	kdb_printf("machine    %s\n", init_uts_ns.name.machine);
	kdb_printf("nodename   %s\n", init_uts_ns.name.nodename);
	kdb_printf("domainname %s\n", init_uts_ns.name.domainname);

	now = __ktime_get_real_seconds();
	kdb_printf("date       %ptTs tz_minuteswest %d\n", &now, sys_tz.tz_minuteswest);
	kdb_sysinfo(&val);
	kdb_printf("uptime     ");
	if (val.uptime > (24*60*60)) {
		int days = val.uptime / (24*60*60);
		val.uptime %= (24*60*60);
		kdb_printf("%d day%s ", days, days == 1 ? "" : "s");
	}
	kdb_printf("%02ld:%02ld\n", val.uptime/(60*60), (val.uptime/60)%60);

	kdb_printf("load avg   %ld.%02ld %ld.%02ld %ld.%02ld\n",
		LOAD_INT(val.loads[0]), LOAD_FRAC(val.loads[0]),
		LOAD_INT(val.loads[1]), LOAD_FRAC(val.loads[1]),
		LOAD_INT(val.loads[2]), LOAD_FRAC(val.loads[2]));

	/* Display in kilobytes */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	kdb_printf("\nMemTotal:       %8lu kB\nMemFree:        %8lu kB\n"
		   "Buffers:        %8lu kB\n",
		   K(val.totalram), K(val.freeram), K(val.bufferram));
	return 0;
}

/*
 * kdb_per_cpu - This function implements the 'per_cpu' command.
 */
static int kdb_per_cpu(int argc, const char **argv)
{
	char fmtstr[64];
	int cpu, diag, nextarg = 1;
	unsigned long addr, symaddr, val, bytesperword = 0, whichcpu = ~0UL;

	if (argc < 1 || argc > 3)
		return KDB_ARGCOUNT;

	diag = kdbgetaddrarg(argc, argv, &nextarg, &symaddr, NULL, NULL);
	if (diag)
		return diag;

	if (argc >= 2) {
		diag = kdbgetularg(argv[2], &bytesperword);
		if (diag)
			return diag;
	}
	if (!bytesperword)
		bytesperword = KDB_WORD_SIZE;
	else if (bytesperword > KDB_WORD_SIZE)
		return KDB_BADWIDTH;
	sprintf(fmtstr, "%%0%dlx ", (int)(2*bytesperword));
	if (argc >= 3) {
		diag = kdbgetularg(argv[3], &whichcpu);
		if (diag)
			return diag;
		if (whichcpu >= nr_cpu_ids || !cpu_online(whichcpu)) {
			kdb_printf("cpu %ld is not online\n", whichcpu);
			return KDB_BADCPUNUM;
		}
	}

	/* Most architectures use __per_cpu_offset[cpu], some use
	 * __per_cpu_offset(cpu), smp has no __per_cpu_offset.
	 */
#ifdef	__per_cpu_offset
#define KDB_PCU(cpu) __per_cpu_offset(cpu)
#else
#ifdef	CONFIG_SMP
#define KDB_PCU(cpu) __per_cpu_offset[cpu]
#else
#define KDB_PCU(cpu) 0
#endif
#endif
	for_each_online_cpu(cpu) {
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;

		if (whichcpu != ~0UL && whichcpu != cpu)
			continue;
		addr = symaddr + KDB_PCU(cpu);
		diag = kdb_getword(&val, addr, bytesperword);
		if (diag) {
			kdb_printf("%5d " kdb_bfd_vma_fmt0 " - unable to "
				   "read, diag=%d\n", cpu, addr, diag);
			continue;
		}
		kdb_printf("%5d ", cpu);
		kdb_md_line(fmtstr, addr,
			bytesperword == KDB_WORD_SIZE,
			1, bytesperword, 1, 1, 0);
	}
#undef KDB_PCU
	return 0;
}

/*
 * display help for the use of cmd | grep pattern
 */
static int kdb_grep_help(int argc, const char **argv)
{
	kdb_printf("Usage of  cmd args | grep pattern:\n");
	kdb_printf("  Any command's output may be filtered through an ");
	kdb_printf("emulated 'pipe'.\n");
	kdb_printf("  'grep' is just a key word.\n");
	kdb_printf("  The pattern may include a very limited set of "
		   "metacharacters:\n");
	kdb_printf("   pattern or ^pattern or pattern$ or ^pattern$\n");
	kdb_printf("  And if there are spaces in the pattern, you may "
		   "quote it:\n");
	kdb_printf("   \"pat tern\" or \"^pat tern\" or \"pat tern$\""
		   " or \"^pat tern$\"\n");
	return 0;
}

/**
 * kdb_register() - This function is used to register a kernel debugger
 *                  command.
 * @cmd: pointer to kdb command
 *
 * Note that it's the job of the caller to keep the memory for the cmd
 * allocated until unregister is called.
 */
int kdb_register(kdbtab_t *cmd)
{
	kdbtab_t *kp;

	list_for_each_entry(kp, &kdb_cmds_head, list_node) {
		if (strcmp(kp->name, cmd->name) == 0) {
			kdb_printf("Duplicate kdb cmd: %s, func %p help %s\n",
				   cmd->name, cmd->func, cmd->help);
			return 1;
		}
	}

	list_add_tail(&cmd->list_node, &kdb_cmds_head);
	return 0;
}
EXPORT_SYMBOL_GPL(kdb_register);

/**
 * kdb_register_table() - This function is used to register a kdb command
 *                        table.
 * @kp: pointer to kdb command table
 * @len: length of kdb command table
 */
void kdb_register_table(kdbtab_t *kp, size_t len)
{
	while (len--) {
		list_add_tail(&kp->list_node, &kdb_cmds_head);
		kp++;
	}
}

/**
 * kdb_unregister() - This function is used to unregister a kernel debugger
 *                    command. It is generally called when a module which
 *                    implements kdb command is unloaded.
 * @cmd: pointer to kdb command
 */
void kdb_unregister(kdbtab_t *cmd)
{
	list_del(&cmd->list_node);
}
EXPORT_SYMBOL_GPL(kdb_unregister);

static kdbtab_t maintab[] = {
	{	.name = "md",
		.func = kdb_md,
		.usage = "<vaddr>",
		.help = "Display Memory Contents, also mdWcN, e.g. md8c1",
		.minlen = 1,
		.flags = KDB_ENABLE_MEM_READ | KDB_REPEAT_NO_ARGS,
	},
	{	.name = "mdr",
		.func = kdb_md,
		.usage = "<vaddr> <bytes>",
		.help = "Display Raw Memory",
		.flags = KDB_ENABLE_MEM_READ | KDB_REPEAT_NO_ARGS,
	},
	{	.name = "mdp",
		.func = kdb_md,
		.usage = "<paddr> <bytes>",
		.help = "Display Physical Memory",
		.flags = KDB_ENABLE_MEM_READ | KDB_REPEAT_NO_ARGS,
	},
	{	.name = "mds",
		.func = kdb_md,
		.usage = "<vaddr>",
		.help = "Display Memory Symbolically",
		.flags = KDB_ENABLE_MEM_READ | KDB_REPEAT_NO_ARGS,
	},
	{	.name = "mm",
		.func = kdb_mm,
		.usage = "<vaddr> <contents>",
		.help = "Modify Memory Contents",
		.flags = KDB_ENABLE_MEM_WRITE | KDB_REPEAT_NO_ARGS,
	},
	{	.name = "go",
		.func = kdb_go,
		.usage = "[<vaddr>]",
		.help = "Continue Execution",
		.minlen = 1,
		.flags = KDB_ENABLE_REG_WRITE |
			     KDB_ENABLE_ALWAYS_SAFE_NO_ARGS,
	},
	{	.name = "rd",
		.func = kdb_rd,
		.usage = "",
		.help = "Display Registers",
		.flags = KDB_ENABLE_REG_READ,
	},
	{	.name = "rm",
		.func = kdb_rm,
		.usage = "<reg> <contents>",
		.help = "Modify Registers",
		.flags = KDB_ENABLE_REG_WRITE,
	},
	{	.name = "ef",
		.func = kdb_ef,
		.usage = "<vaddr>",
		.help = "Display exception frame",
		.flags = KDB_ENABLE_MEM_READ,
	},
	{	.name = "bt",
		.func = kdb_bt,
		.usage = "[<vaddr>]",
		.help = "Stack traceback",
		.minlen = 1,
		.flags = KDB_ENABLE_MEM_READ | KDB_ENABLE_INSPECT_NO_ARGS,
	},
	{	.name = "btp",
		.func = kdb_bt,
		.usage = "<pid>",
		.help = "Display stack for process <pid>",
		.flags = KDB_ENABLE_INSPECT,
	},
	{	.name = "bta",
		.func = kdb_bt,
		.usage = "[<state_chars>|A]",
		.help = "Backtrace all processes whose state matches",
		.flags = KDB_ENABLE_INSPECT,
	},
	{	.name = "btc",
		.func = kdb_bt,
		.usage = "",
		.help = "Backtrace current process on each cpu",
		.flags = KDB_ENABLE_INSPECT,
	},
	{	.name = "btt",
		.func = kdb_bt,
		.usage = "<vaddr>",
		.help = "Backtrace process given its struct task address",
		.flags = KDB_ENABLE_MEM_READ | KDB_ENABLE_INSPECT_NO_ARGS,
	},
	{	.name = "env",
		.func = kdb_env,
		.usage = "",
		.help = "Show environment variables",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "set",
		.func = kdb_set,
		.usage = "",
		.help = "Set environment variables",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "help",
		.func = kdb_help,
		.usage = "",
		.help = "Display Help Message",
		.minlen = 1,
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "?",
		.func = kdb_help,
		.usage = "",
		.help = "Display Help Message",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "cpu",
		.func = kdb_cpu,
		.usage = "<cpunum>",
		.help = "Switch to new cpu",
		.flags = KDB_ENABLE_ALWAYS_SAFE_NO_ARGS,
	},
	{	.name = "kgdb",
		.func = kdb_kgdb,
		.usage = "",
		.help = "Enter kgdb mode",
		.flags = 0,
	},
	{	.name = "ps",
		.func = kdb_ps,
		.usage = "[<state_chars>|A]",
		.help = "Display active task list",
		.flags = KDB_ENABLE_INSPECT,
	},
	{	.name = "pid",
		.func = kdb_pid,
		.usage = "<pidnum>",
		.help = "Switch to another task",
		.flags = KDB_ENABLE_INSPECT,
	},
	{	.name = "reboot",
		.func = kdb_reboot,
		.usage = "",
		.help = "Reboot the machine immediately",
		.flags = KDB_ENABLE_REBOOT,
	},
#if defined(CONFIG_MODULES)
	{	.name = "lsmod",
		.func = kdb_lsmod,
		.usage = "",
		.help = "List loaded kernel modules",
		.flags = KDB_ENABLE_INSPECT,
	},
#endif
#if defined(CONFIG_MAGIC_SYSRQ)
	{	.name = "sr",
		.func = kdb_sr,
		.usage = "<key>",
		.help = "Magic SysRq key",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
#endif
#if defined(CONFIG_PRINTK)
	{	.name = "dmesg",
		.func = kdb_dmesg,
		.usage = "[lines]",
		.help = "Display syslog buffer",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
#endif
	{	.name = "defcmd",
		.func = kdb_defcmd,
		.usage = "name \"usage\" \"help\"",
		.help = "Define a set of commands, down to endefcmd",
		/*
		 * Macros are always safe because when executed each
		 * internal command re-enters kdb_parse() and is safety
		 * checked individually.
		 */
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "kill",
		.func = kdb_kill,
		.usage = "<-signal> <pid>",
		.help = "Send a signal to a process",
		.flags = KDB_ENABLE_SIGNAL,
	},
	{	.name = "summary",
		.func = kdb_summary,
		.usage = "",
		.help = "Summarize the system",
		.minlen = 4,
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
	{	.name = "per_cpu",
		.func = kdb_per_cpu,
		.usage = "<sym> [<bytes>] [<cpu>]",
		.help = "Display per_cpu variables",
		.minlen = 3,
		.flags = KDB_ENABLE_MEM_READ,
	},
	{	.name = "grephelp",
		.func = kdb_grep_help,
		.usage = "",
		.help = "Display help on | grep",
		.flags = KDB_ENABLE_ALWAYS_SAFE,
	},
};

static kdbtab_t nmicmd = {
	.name = "disable_nmi",
	.func = kdb_disable_nmi,
	.usage = "",
	.help = "Disable NMI entry to KDB",
	.flags = KDB_ENABLE_ALWAYS_SAFE,
};

/* Initialize the kdb command table. */
static void __init kdb_inittab(void)
{
	kdb_register_table(maintab, ARRAY_SIZE(maintab));
	if (arch_kgdb_ops.enable_nmi)
		kdb_register_table(&nmicmd, 1);
}

/* Execute any commands defined in kdb_cmds.  */
static void __init kdb_cmd_init(void)
{
	int i, diag;
	for (i = 0; kdb_cmds[i]; ++i) {
		diag = kdb_parse(kdb_cmds[i]);
		if (diag)
			kdb_printf("kdb command %s failed, kdb diag %d\n",
				kdb_cmds[i], diag);
	}
	if (defcmd_in_progress) {
		kdb_printf("Incomplete 'defcmd' set, forcing endefcmd\n");
		kdb_parse("endefcmd");
	}
}

/* Initialize kdb_printf, breakpoint tables and kdb state */
void __init kdb_init(int lvl)
{
	static int kdb_init_lvl = KDB_NOT_INITIALIZED;
	int i;

	if (kdb_init_lvl == KDB_INIT_FULL || lvl <= kdb_init_lvl)
		return;
	for (i = kdb_init_lvl; i < lvl; i++) {
		switch (i) {
		case KDB_NOT_INITIALIZED:
			kdb_inittab();		/* Initialize Command Table */
			kdb_initbptab();	/* Initialize Breakpoints */
			break;
		case KDB_INIT_EARLY:
			kdb_cmd_init();		/* Build kdb_cmds tables */
			break;
		}
	}
	kdb_init_lvl = lvl;
}
