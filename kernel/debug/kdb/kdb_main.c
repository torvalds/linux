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
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/smp.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
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
#include "kdb_private.h"

#define GREP_LEN 256
char kdb_grep_string[GREP_LEN];
int kdb_grepping_flag;
EXPORT_SYMBOL(kdb_grepping_flag);
int kdb_grep_leading;
int kdb_grep_trailing;

/*
 * Kernel debugger state flags
 */
int kdb_flags;
atomic_t kdb_event;

/*
 * kdb_lock protects updates to kdb_initial_cpu.  Used to
 * single thread processors through the kernel debugger.
 */
int kdb_initial_cpu = -1;	/* cpu number that owns kdb */
int kdb_nextline = 1;
int kdb_state;			/* General KDB state */

struct task_struct *kdb_current_task;
EXPORT_SYMBOL(kdb_current_task);
struct pt_regs *kdb_current_regs;

const char *kdb_diemsg;
static int kdb_go_count;
#ifdef CONFIG_KDB_CONTINUE_CATASTROPHIC
static unsigned int kdb_continue_catastrophic =
	CONFIG_KDB_CONTINUE_CATASTROPHIC;
#else
static unsigned int kdb_continue_catastrophic;
#endif

/* kdb_commands describes the available commands. */
static kdbtab_t *kdb_commands;
#define KDB_BASE_CMD_MAX 50
static int kdb_max_commands = KDB_BASE_CMD_MAX;
static kdbtab_t kdb_base_commands[50];
#define for_each_kdbcmd(cmd, num)					\
	for ((cmd) = kdb_base_commands, (num) = 0;			\
	     num < kdb_max_commands;					\
	     num == KDB_BASE_CMD_MAX ? cmd = kdb_commands : cmd++, num++)

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
};
#undef KDBMSG

static const int __nkdb_err = sizeof(kdbmsgs) / sizeof(kdbmsg_t);


/*
 * Initial environment.   This is all kept static and local to
 * this file.   We don't want to rely on the memory allocation
 * mechanisms in the kernel, so we use a very limited allocate-only
 * heap for new and altered environment variables.  The entire
 * environment is limited to a fixed number of entries (add more
 * to __env[] if required) and a fixed amount of heap (add more to
 * KDB_ENVBUFSIZE if required).
 */

static char *__env[] = {
#if defined(CONFIG_SMP)
 "PROMPT=[%d]kdb> ",
 "MOREPROMPT=[%d]more> ",
#else
 "PROMPT=kdb> ",
 "MOREPROMPT=more> ",
#endif
 "RADIX=16",
 "MDCOUNT=8",			/* lines of md output */
 "BTARGS=9",			/* 9 possible args in bt */
 KDB_PLATFORM_ENV,
 "DTABCOUNT=30",
 "NOSECT=1",
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
 (char *)0,
};

static const int __nenv = (sizeof(__env) / sizeof(char *));

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
 *	match	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long representation of the env variable 'match'
 * Returns:
 *	Zero on success, a kdb diagnostic on failure.
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
 *	*value  the unsigned long represntation of the env variable 'match'
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
 * kdbgetularg - This function will convert a numeric string into an
 *	unsigned long value.
 * Parameters:
 *	arg	A character string representing a numeric value
 * Outputs:
 *	*value  the unsigned long represntation of arg.
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
	int i;
	char *ep;
	size_t varlen, vallen;

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
		kdb_flags = (kdb_flags &
			     ~(KDB_DEBUG_FLAG_MASK << KDB_DEBUG_FLAG_SHIFT))
			| (debugflags << KDB_DEBUG_FLAG_SHIFT);

		return 0;
	}

	/*
	 * Tokenizer squashed the '=' sign.  argv[1] is variable
	 * name, argv[2] = value.
	 */
	varlen = strlen(argv[1]);
	vallen = strlen(argv[2]);
	ep = kdballocenv(varlen + vallen + 2);
	if (ep == (char *)0)
		return KDB_ENVBUFFULL;

	sprintf(ep, "%s=%s", argv[1], argv[2]);

	ep[varlen+vallen+1] = '\0';

	for (i = 0; i < __nenv; i++) {
		if (__env[i]
		 && ((strncmp(__env[i], argv[1], varlen) == 0)
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
 *	hexidecimal), a symbol name, a register name (preceeded by the
 *	percent sign), an environment variable with a numeric value
 *	(preceeded by a dollar sign) or a simple arithmetic expression
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
struct defcmd_set {
	int count;
	int usable;
	char *name;
	char *usage;
	char *help;
	char **command;
};
static struct defcmd_set *defcmd_set;
static int defcmd_set_count;
static int defcmd_in_progress;

/* Forward references */
static int kdb_exec_defcmd(int argc, const char **argv);

static int kdb_defcmd2(const char *cmdstr, const char *argv0)
{
	struct defcmd_set *s = defcmd_set + defcmd_set_count - 1;
	char **save_command = s->command;
	if (strcmp(argv0, "endefcmd") == 0) {
		defcmd_in_progress = 0;
		if (!s->count)
			s->usable = 0;
		if (s->usable)
			kdb_register(s->name, kdb_exec_defcmd,
				     s->usage, s->help, 0);
		return 0;
	}
	if (!s->usable)
		return KDB_NOTIMP;
	s->command = kmalloc((s->count + 1) * sizeof(*(s->command)), GFP_KDB);
	if (!s->command) {
		kdb_printf("Could not allocate new kdb_defcmd table for %s\n",
			   cmdstr);
		s->usable = 0;
		return KDB_NOTIMP;
	}
	memcpy(s->command, save_command, s->count * sizeof(*(s->command)));
	s->command[s->count++] = kdb_strdup(cmdstr, GFP_KDB);
	kfree(save_command);
	return 0;
}

static int kdb_defcmd(int argc, const char **argv)
{
	struct defcmd_set *save_defcmd_set = defcmd_set, *s;
	if (defcmd_in_progress) {
		kdb_printf("kdb: nested defcmd detected, assuming missing "
			   "endefcmd\n");
		kdb_defcmd2("endefcmd", "endefcmd");
	}
	if (argc == 0) {
		int i;
		for (s = defcmd_set; s < defcmd_set + defcmd_set_count; ++s) {
			kdb_printf("defcmd %s \"%s\" \"%s\"\n", s->name,
				   s->usage, s->help);
			for (i = 0; i < s->count; ++i)
				kdb_printf("%s", s->command[i]);
			kdb_printf("endefcmd\n");
		}
		return 0;
	}
	if (argc != 3)
		return KDB_ARGCOUNT;
	defcmd_set = kmalloc((defcmd_set_count + 1) * sizeof(*defcmd_set),
			     GFP_KDB);
	if (!defcmd_set) {
		kdb_printf("Could not allocate new defcmd_set entry for %s\n",
			   argv[1]);
		defcmd_set = save_defcmd_set;
		return KDB_NOTIMP;
	}
	memcpy(defcmd_set, save_defcmd_set,
	       defcmd_set_count * sizeof(*defcmd_set));
	kfree(save_defcmd_set);
	s = defcmd_set + defcmd_set_count;
	memset(s, 0, sizeof(*s));
	s->usable = 1;
	s->name = kdb_strdup(argv[1], GFP_KDB);
	s->usage = kdb_strdup(argv[2], GFP_KDB);
	s->help = kdb_strdup(argv[3], GFP_KDB);
	if (s->usage[0] == '"') {
		strcpy(s->usage, s->usage+1);
		s->usage[strlen(s->usage)-1] = '\0';
	}
	if (s->help[0] == '"') {
		strcpy(s->help, s->help+1);
		s->help[strlen(s->help)-1] = '\0';
	}
	++defcmd_set_count;
	defcmd_in_progress = 1;
	return 0;
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
	int i, ret;
	struct defcmd_set *s;
	if (argc != 0)
		return KDB_ARGCOUNT;
	for (s = defcmd_set, i = 0; i < defcmd_set_count; ++i, ++s) {
		if (strcmp(s->name, argv[0]) == 0)
			break;
	}
	if (i == defcmd_set_count) {
		kdb_printf("kdb_exec_defcmd: could not find commands for %s\n",
			   argv[0]);
		return KDB_NOTIMP;
	}
	for (i = 0; i < s->count; ++i) {
		/* Recursive use of kdb_parse, do not use argv after
		 * this point */
		argv = NULL;
		kdb_printf("[%s]kdb> %s\n", s->name, s->command[i]);
		ret = kdb_parse(s->command[i]);
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
	if (strncmp(cp, "grep ", 5)) {
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
	if (len >= GREP_LEN) {
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
 *	is considered a token delimeter (but special consideration
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
	int i, escaped, ignore_errors = 0, check_grep;

	/*
	 * First tokenize the command string.
	 */
	cp = (char *)cmdstr;
	kdb_grepping_flag = check_grep = 0;

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

	for_each_kdbcmd(tp, i) {
		if (tp->cmd_name) {
			/*
			 * If this command is allowed to be abbreviated,
			 * check to see if this is it.
			 */

			if (tp->cmd_minlen
			 && (strlen(argv[0]) <= tp->cmd_minlen)) {
				if (strncmp(argv[0],
					    tp->cmd_name,
					    tp->cmd_minlen) == 0) {
					break;
				}
			}

			if (strcmp(argv[0], tp->cmd_name) == 0)
				break;
		}
	}

	/*
	 * If we don't find a command by this name, see if the first
	 * few characters of this match any of the known commands.
	 * e.g., md1c20 should match md.
	 */
	if (i == kdb_max_commands) {
		for_each_kdbcmd(tp, i) {
			if (tp->cmd_name) {
				if (strncmp(argv[0],
					    tp->cmd_name,
					    strlen(tp->cmd_name)) == 0) {
					break;
				}
			}
		}
	}

	if (i < kdb_max_commands) {
		int result;
		KDB_STATE_SET(CMD);
		result = (*tp->cmd_func)(argc-1, (const char **)argv);
		if (result && ignore_errors && result > KDB_CMD_GO)
			result = 0;
		KDB_STATE_CLEAR(CMD);
		switch (tp->cmd_repeat) {
		case KDB_REPEAT_NONE:
			argc = 0;
			if (argv[0])
				*(argv[0]) = '\0';
			break;
		case KDB_REPEAT_NO_ARGS:
			argc = 1;
			if (argv[1])
				*(argv[1]) = '\0';
			break;
		case KDB_REPEAT_WITH_ARGS:
			break;
		}
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
			cmdptr = (cmdptr-1) % KDB_CMD_HISTORY_COUNT;
		strncpy(cmd_cur, cmd_hist[cmdptr], CMD_BUFLEN);
		return 1;
	case CTRL_N:
		if (cmdptr != cmd_head)
			cmdptr = (cmdptr+1) % KDB_CMD_HISTORY_COUNT;
		strncpy(cmd_cur, cmd_hist[cmdptr], CMD_BUFLEN);
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
	console_loglevel = 15;
	kdb_trap_printk++;
	show_regs(regs);
	kdb_trap_printk--;
	kdb_printf("\n");
	console_loglevel = old_lvl;
}

void kdb_set_current_task(struct task_struct *p)
{
	kdb_current_task = p;

	if (kdb_task_has_cpu(p)) {
		kdb_current_regs = KDB_TSKREGS(kdb_process_cpu(p));
		return;
	}
	kdb_current_regs = NULL;
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
 *	KDB_CMD_SSB	Single step until branch.
 */
static int kdb_local(kdb_reason_t reason, int error, struct pt_regs *regs,
		     kdb_dbtrap_t db_result)
{
	char *cmdbuf;
	int diag;
	struct task_struct *kdb_current =
		kdb_curr_task(raw_smp_processor_id());

	KDB_DEBUG_STATE("kdb_local 1", reason);
	kdb_go_count = 0;
	if (reason == KDB_REASON_DEBUG) {
		/* special case below */
	} else {
		kdb_printf("\nEntering kdb (current=0x%p, pid %d) ",
			   kdb_current, kdb_current->pid);
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
			kdb_printf("\nEntering kdb (0x%p, pid %d) ",
				   kdb_current, kdb_current->pid);
#if defined(CONFIG_SMP)
			kdb_printf("on processor %d ", raw_smp_processor_id());
#endif
			kdb_printf("due to Debug @ " kdb_machreg_fmt "\n",
				   instruction_pointer(regs));
			break;
		case KDB_DB_SSB:
			/*
			 * In the midst of ssb command. Just return.
			 */
			KDB_DEBUG_STATE("kdb_local 3", reason);
			return KDB_CMD_SSB;	/* Continue with SSB command */

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
	case KDB_REASON_NMI:
		kdb_printf("due to NonMaskable Interrupt @ "
			   kdb_machreg_fmt "\n",
			   instruction_pointer(regs));
		kdb_dumpregs(regs);
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

		cmdbuf = cmd_cur;
		*cmdbuf = '\0';
		*(cmd_hist[cmd_head]) = '\0';

		if (KDB_FLAG(ONLY_DO_DUMP)) {
			/* kdb is off but a catastrophic error requires a dump.
			 * Take the dump and reboot.
			 * Turn on logging so the kdb output appears in the log
			 * buffer in the dump.
			 */
			const char *setargs[] = { "set", "LOGGING", "1" };
			kdb_set(2, setargs);
			kdb_reboot(0, NULL);
			/*NOTREACHED*/
		}

do_full_getstr:
#if defined(CONFIG_SMP)
		snprintf(kdb_prompt_str, CMD_BUFLEN, kdbgetenv("PROMPT"),
			 raw_smp_processor_id());
#else
		snprintf(kdb_prompt_str, CMD_BUFLEN, kdbgetenv("PROMPT"));
#endif
		if (defcmd_in_progress)
			strncat(kdb_prompt_str, "[defcmd]", CMD_BUFLEN);

		/*
		 * Fetch command from keyboard
		 */
		cmdbuf = kdb_getstr(cmdbuf, CMD_BUFLEN, kdb_prompt_str);
		if (*cmdbuf != '\n') {
			if (*cmdbuf < 32) {
				if (cmdptr == cmd_head) {
					strncpy(cmd_hist[cmd_head], cmd_cur,
						CMD_BUFLEN);
					*(cmd_hist[cmd_head] +
					  strlen(cmd_hist[cmd_head])-1) = '\0';
				}
				if (!handle_ctrl_cmd(cmdbuf))
					*(cmd_cur+strlen(cmd_cur)-1) = '\0';
				cmdbuf = cmd_cur;
				goto do_full_getstr;
			} else {
				strncpy(cmd_hist[cmd_head], cmd_cur,
					CMD_BUFLEN);
			}

			cmd_head = (cmd_head+1) % KDB_CMD_HISTORY_COUNT;
			if (cmd_head == cmd_tail)
				cmd_tail = (cmd_tail+1) % KDB_CMD_HISTORY_COUNT;
		}

		cmdptr = cmd_head;
		diag = kdb_parse(cmdbuf);
		if (diag == KDB_NOTFOUND) {
			kdb_printf("Unknown kdb command: '%s'\n", cmdbuf);
			diag = 0;
		}
		if (diag == KDB_CMD_GO
		 || diag == KDB_CMD_CPU
		 || diag == KDB_CMD_SS
		 || diag == KDB_CMD_SSB
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
 *			acording to kdb state.
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

		if (result == KDB_CMD_SSB) {
			KDB_STATE_SET(DOING_SS);
			KDB_STATE_SET(DOING_SSB);
			break;
		}

		if (result == KDB_CMD_KGDB) {
			if (!(KDB_STATE(DOING_KGDB) || KDB_STATE(DOING_KGDB2)))
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
			switch (bytesperword) {
			case 8:
				*c++ = printable_char(*cp++);
				*c++ = printable_char(*cp++);
				*c++ = printable_char(*cp++);
				*c++ = printable_char(*cp++);
				addr += 4;
			case 4:
				*c++ = printable_char(*cp++);
				*c++ = printable_char(*cp++);
				addr += 2;
			case 2:
				*c++ = printable_char(*cp++);
				addr++;
			case 1:
				*c++ = printable_char(*cp++);
				addr++;
				break;
			}
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

	kdbgetintenv("MDCOUNT", &mdcount);
	kdbgetintenv("RADIX", &radix);
	kdbgetintenv("BYTESPERWORD", &bytesperword);

	/* Assume 'md <addr>' and start with environment values */
	repeat = mdcount * 16 / bytesperword;

	if (strcmp(argv[0], "mdr") == 0) {
		if (argc != 2)
			return KDB_ARGCOUNT;
		valid = 1;
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
				repeat = mdcount * 16 / bytesperword;
			}
		}
		if (argc >= nextarg+1) {
			diag = kdbgetularg(argv[nextarg+1], &val);
			if (!diag)
				radix = (int) val;
		}
	}

	if (strcmp(argv[0], "mdr") == 0)
		return kdb_mdr(addr, mdcount);

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

	if (argc == 1) {
		if (raw_smp_processor_id() != kdb_initial_cpu) {
			kdb_printf("go <address> must be issued from the "
				   "initial cpu, do cpu %d first\n",
				   kdb_initial_cpu);
			return KDB_ARGCOUNT;
		}
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
	if (argc != 1)
		return KDB_ARGCOUNT;
	kdb_trap_printk++;
	__handle_sysrq(*argv[1], false);
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

#if defined(CONFIG_MODULES)
/*
 * kdb_lsmod - This function implements the 'lsmod' command.  Lists
 *	currently loaded kernel modules.
 *	Mostly taken from userland lsmod.
 */
static int kdb_lsmod(int argc, const char **argv)
{
	struct module *mod;

	if (argc != 0)
		return KDB_ARGCOUNT;

	kdb_printf("Module                  Size  modstruct     Used by\n");
	list_for_each_entry(mod, kdb_modules, list) {

		kdb_printf("%-20s%8u  0x%p ", mod->name,
			   mod->core_size, (void *)mod);
#ifdef CONFIG_MODULE_UNLOAD
		kdb_printf("%4d ", module_refcount(mod));
#endif
		if (mod->state == MODULE_STATE_GOING)
			kdb_printf(" (Unloading)");
		else if (mod->state == MODULE_STATE_COMING)
			kdb_printf(" (Loading)");
		else
			kdb_printf(" (Live)");
		kdb_printf(" 0x%p", mod->module_core);

#ifdef CONFIG_MODULE_UNLOAD
		{
			struct module_use *use;
			kdb_printf(" [ ");
			list_for_each_entry(use, &mod->source_list,
					    source_list)
				kdb_printf("%s ", use->target->name);
			kdb_printf("]\n");
		}
#endif
	}

	return 0;
}

#endif	/* CONFIG_MODULES */

/*
 * kdb_env - This function implements the 'env' command.  Display the
 *	current environment variables.
 */

static int kdb_env(int argc, const char **argv)
{
	int i;

	for (i = 0; i < __nenv; i++) {
		if (__env[i])
			kdb_printf("%s\n", __env[i]);
	}

	if (KDB_DEBUG(MASK))
		kdb_printf("KDBFLAGS=0x%x\n", kdb_flags);

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
	char *syslog_data[4], *start, *end, c = '\0', *p;
	int diag, logging, logsize, lines = 0, adjust = 0, n;

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

	/* syslog_data[0,1] physical start, end+1.  syslog_data[2,3]
	 * logical start, end+1. */
	kdb_syslog_data(syslog_data);
	if (syslog_data[2] == syslog_data[3])
		return 0;
	logsize = syslog_data[1] - syslog_data[0];
	start = syslog_data[2];
	end = syslog_data[3];
#define KDB_WRAP(p) (((p - syslog_data[0]) % logsize) + syslog_data[0])
	for (n = 0, p = start; p < end; ++p) {
		c = *KDB_WRAP(p);
		if (c == '\n')
			++n;
	}
	if (c != '\n')
		++n;
	if (lines < 0) {
		if (adjust >= n)
			kdb_printf("buffer only contains %d lines, nothing "
				   "printed\n", n);
		else if (adjust - lines >= n)
			kdb_printf("buffer only contains %d lines, last %d "
				   "lines printed\n", n, n - adjust);
		if (adjust) {
			for (; start < end && adjust; ++start) {
				if (*KDB_WRAP(start) == '\n')
					--adjust;
			}
			if (start < end)
				++start;
		}
		for (p = start; p < end && lines; ++p) {
			if (*KDB_WRAP(p) == '\n')
				++lines;
		}
		end = p;
	} else if (lines > 0) {
		int skip = n - (adjust + lines);
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
		for (; start < end && skip; ++start) {
			if (*KDB_WRAP(start) == '\n')
				--skip;
		}
		for (p = start; p < end && lines; ++p) {
			if (*KDB_WRAP(p) == '\n')
				--lines;
		}
		end = p;
	}
	/* Do a line at a time (max 200 chars) to reduce protocol overhead */
	c = '\n';
	while (start != end) {
		char buf[201];
		p = buf;
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		while (start < end && (c = *KDB_WRAP(start)) &&
		       (p - buf) < sizeof(buf)-1) {
			++start;
			*p++ = c;
			if (c == '\n')
				break;
		}
		*p = '\0';
		kdb_printf("%s", buf);
	}
	if (c != '\n')
		kdb_printf("\n");

	return 0;
}
#endif /* CONFIG_PRINTK */
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
		} else {
			state = ' ';	/* cpu is responding to kdb */
			if (kdb_task_state_char(KDB_TSK(i)) == 'I')
				state = 'I';	/* idle task */
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
	if ((cpunum > NR_CPUS) || !cpu_online(cpunum))
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
	unsigned long mask_I = kdb_task_state_string("I"),
		      mask_M = kdb_task_state_string("M");
	unsigned long cpu;
	const struct task_struct *p, *g;
	for_each_online_cpu(cpu) {
		p = kdb_curr_task(cpu);
		if (kdb_task_state(p, mask_I))
			++idle;
	}
	kdb_do_each_thread(g, p) {
		if (kdb_task_state(p, mask_M))
			++daemon;
	} kdb_while_each_thread(g, p);
	if (idle || daemon) {
		if (idle)
			kdb_printf("%d idle process%s (state I)%s\n",
				   idle, idle == 1 ? "" : "es",
				   daemon ? " and " : "");
		if (daemon)
			kdb_printf("%d sleeping system daemon (state M) "
				   "process%s", daemon,
				   daemon == 1 ? "" : "es");
		kdb_printf(" suppressed,\nuse 'ps A' to see all.\n");
	}
}

/*
 * kdb_ps - This function implements the 'ps' command which shows a
 *	list of the active processes.
 *		ps [DRSTCZEUIMA]   All processes, optionally filtered by state
 */
void kdb_ps1(const struct task_struct *p)
{
	int cpu;
	unsigned long tmp;

	if (!p || probe_kernel_read(&tmp, (char *)p, sizeof(unsigned long)))
		return;

	cpu = kdb_process_cpu(p);
	kdb_printf("0x%p %8d %8d  %d %4d   %c  0x%p %c%s\n",
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
				   "process table (0x%p)\n", KDB_TSK(cpu));
		}
	}
}

static int kdb_ps(int argc, const char **argv)
{
	struct task_struct *g, *p;
	unsigned long mask, cpu;

	if (argc == 0)
		kdb_ps_suppressed();
	kdb_printf("%-*s      Pid   Parent [*] cpu State %-*s Command\n",
		(int)(2*sizeof(void *))+2, "Task Addr",
		(int)(2*sizeof(void *))+2, "Thread");
	mask = kdb_task_state_string(argc ? argv[1] : NULL);
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
	kdb_do_each_thread(g, p) {
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
		if (kdb_task_state(p, mask))
			kdb_ps1(p);
	} kdb_while_each_thread(g, p);

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

/*
 * kdb_ll - This function implements the 'll' command which follows a
 *	linked list and executes an arbitrary command for each
 *	element.
 */
static int kdb_ll(int argc, const char **argv)
{
	int diag;
	unsigned long addr;
	long offset = 0;
	unsigned long va;
	unsigned long linkoffset;
	int nextarg;
	const char *command;

	if (argc != 3)
		return KDB_ARGCOUNT;

	nextarg = 1;
	diag = kdbgetaddrarg(argc, argv, &nextarg, &addr, &offset, NULL);
	if (diag)
		return diag;

	diag = kdbgetularg(argv[2], &linkoffset);
	if (diag)
		return diag;

	/*
	 * Using the starting address as
	 * the first element in the list, and assuming that
	 * the list ends with a null pointer.
	 */

	va = addr;
	command = kdb_strdup(argv[3], GFP_KDB);
	if (!command) {
		kdb_printf("%s: cannot duplicate command\n", __func__);
		return 0;
	}
	/* Recursive use of kdb_parse, do not use argv after this point */
	argv = NULL;

	while (va) {
		char buf[80];

		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;

		sprintf(buf, "%s " kdb_machreg_fmt "\n", command, va);
		diag = kdb_parse(buf);
		if (diag)
			return diag;

		addr = va + linkoffset;
		if (kdb_getword(&va, addr, sizeof(va)))
			return 0;
	}
	kfree(command);

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
	int i;

	kdb_printf("%-15.15s %-20.20s %s\n", "Command", "Usage", "Description");
	kdb_printf("-----------------------------"
		   "-----------------------------\n");
	for_each_kdbcmd(kt, i) {
		if (kt->cmd_name)
			kdb_printf("%-15.15s %-20.20s %s\n", kt->cmd_name,
				   kt->cmd_usage, kt->cmd_help);
		if (KDB_FLAG(CMD_INTERRUPT))
			return 0;
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
	struct siginfo info;

	if (argc != 2)
		return KDB_ARGCOUNT;

	sig = simple_strtol(argv[1], &endp, 0);
	if (*endp)
		return KDB_BADINT;
	if (sig >= 0) {
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
	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = pid;  /* same capabilities as process being signalled */
	info.si_uid = 0;    /* kdb has root authority */
	kdb_send_sig_info(p, &info);
	return 0;
}

struct kdb_tm {
	int tm_sec;	/* seconds */
	int tm_min;	/* minutes */
	int tm_hour;	/* hours */
	int tm_mday;	/* day of the month */
	int tm_mon;	/* month */
	int tm_year;	/* year */
};

static void kdb_gmtime(struct timespec *tv, struct kdb_tm *tm)
{
	/* This will work from 1970-2099, 2100 is not a leap year */
	static int mon_day[] = { 31, 29, 31, 30, 31, 30, 31,
				 31, 30, 31, 30, 31 };
	memset(tm, 0, sizeof(*tm));
	tm->tm_sec  = tv->tv_sec % (24 * 60 * 60);
	tm->tm_mday = tv->tv_sec / (24 * 60 * 60) +
		(2 * 365 + 1); /* shift base from 1970 to 1968 */
	tm->tm_min =  tm->tm_sec / 60 % 60;
	tm->tm_hour = tm->tm_sec / 60 / 60;
	tm->tm_sec =  tm->tm_sec % 60;
	tm->tm_year = 68 + 4*(tm->tm_mday / (4*365+1));
	tm->tm_mday %= (4*365+1);
	mon_day[1] = 29;
	while (tm->tm_mday >= mon_day[tm->tm_mon]) {
		tm->tm_mday -= mon_day[tm->tm_mon];
		if (++tm->tm_mon == 12) {
			tm->tm_mon = 0;
			++tm->tm_year;
			mon_day[1] = 28;
		}
	}
	++tm->tm_mday;
}

/*
 * Most of this code has been lifted from kernel/timer.c::sys_sysinfo().
 * I cannot call that code directly from kdb, it has an unconditional
 * cli()/sti() and calls routines that take locks which can stop the debugger.
 */
static void kdb_sysinfo(struct sysinfo *val)
{
	struct timespec uptime;
	do_posix_clock_monotonic_gettime(&uptime);
	memset(val, 0, sizeof(*val));
	val->uptime = uptime.tv_sec;
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
	struct timespec now;
	struct kdb_tm tm;
	struct sysinfo val;

	if (argc)
		return KDB_ARGCOUNT;

	kdb_printf("sysname    %s\n", init_uts_ns.name.sysname);
	kdb_printf("release    %s\n", init_uts_ns.name.release);
	kdb_printf("version    %s\n", init_uts_ns.name.version);
	kdb_printf("machine    %s\n", init_uts_ns.name.machine);
	kdb_printf("nodename   %s\n", init_uts_ns.name.nodename);
	kdb_printf("domainname %s\n", init_uts_ns.name.domainname);
	kdb_printf("ccversion  %s\n", __stringify(CCVERSION));

	now = __current_kernel_time();
	kdb_gmtime(&now, &tm);
	kdb_printf("date       %04d-%02d-%02d %02d:%02d:%02d "
		   "tz_minuteswest %d\n",
		1900+tm.tm_year, tm.tm_mon+1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		sys_tz.tz_minuteswest);

	kdb_sysinfo(&val);
	kdb_printf("uptime     ");
	if (val.uptime > (24*60*60)) {
		int days = val.uptime / (24*60*60);
		val.uptime %= (24*60*60);
		kdb_printf("%d day%s ", days, days == 1 ? "" : "s");
	}
	kdb_printf("%02ld:%02ld\n", val.uptime/(60*60), (val.uptime/60)%60);

	/* lifted from fs/proc/proc_misc.c::loadavg_read_proc() */

#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)
	kdb_printf("load avg   %ld.%02ld %ld.%02ld %ld.%02ld\n",
		LOAD_INT(val.loads[0]), LOAD_FRAC(val.loads[0]),
		LOAD_INT(val.loads[1]), LOAD_FRAC(val.loads[1]),
		LOAD_INT(val.loads[2]), LOAD_FRAC(val.loads[2]));
#undef LOAD_INT
#undef LOAD_FRAC
	/* Display in kilobytes */
#define K(x) ((x) << (PAGE_SHIFT - 10))
	kdb_printf("\nMemTotal:       %8lu kB\nMemFree:        %8lu kB\n"
		   "Buffers:        %8lu kB\n",
		   val.totalram, val.freeram, val.bufferram);
	return 0;
}

/*
 * kdb_per_cpu - This function implements the 'per_cpu' command.
 */
static int kdb_per_cpu(int argc, const char **argv)
{
	char buf[256], fmtstr[64];
	kdb_symtab_t symtab;
	cpumask_t suppress = CPU_MASK_NONE;
	int cpu, diag;
	unsigned long addr, val, bytesperword = 0, whichcpu = ~0UL;

	if (argc < 1 || argc > 3)
		return KDB_ARGCOUNT;

	snprintf(buf, sizeof(buf), "per_cpu__%s", argv[1]);
	if (!kdbgetsymval(buf, &symtab)) {
		kdb_printf("%s is not a per_cpu variable\n", argv[1]);
		return KDB_BADADDR;
	}
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
		if (!cpu_online(whichcpu)) {
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
		if (whichcpu != ~0UL && whichcpu != cpu)
			continue;
		addr = symtab.sym_start + KDB_PCU(cpu);
		diag = kdb_getword(&val, addr, bytesperword);
		if (diag) {
			kdb_printf("%5d " kdb_bfd_vma_fmt0 " - unable to "
				   "read, diag=%d\n", cpu, addr, diag);
			continue;
		}
#ifdef	CONFIG_SMP
		if (!val) {
			cpu_set(cpu, suppress);
			continue;
		}
#endif	/* CONFIG_SMP */
		kdb_printf("%5d ", cpu);
		kdb_md_line(fmtstr, addr,
			bytesperword == KDB_WORD_SIZE,
			1, bytesperword, 1, 1, 0);
	}
	if (cpus_weight(suppress) == 0)
		return 0;
	kdb_printf("Zero suppressed cpu(s):");
	for (cpu = first_cpu(suppress); cpu < num_possible_cpus();
	     cpu = next_cpu(cpu, suppress)) {
		kdb_printf(" %d", cpu);
		if (cpu == num_possible_cpus() - 1 ||
		    next_cpu(cpu, suppress) != cpu + 1)
			continue;
		while (cpu < num_possible_cpus() &&
		       next_cpu(cpu, suppress) == cpu + 1)
			++cpu;
		kdb_printf("-%d", cpu);
	}
	kdb_printf("\n");

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

/*
 * kdb_register_repeat - This function is used to register a kernel
 * 	debugger command.
 * Inputs:
 *	cmd	Command name
 *	func	Function to execute the command
 *	usage	A simple usage string showing arguments
 *	help	A simple help string describing command
 *	repeat	Does the command auto repeat on enter?
 * Returns:
 *	zero for success, one if a duplicate command.
 */
#define kdb_command_extend 50	/* arbitrary */
int kdb_register_repeat(char *cmd,
			kdb_func_t func,
			char *usage,
			char *help,
			short minlen,
			kdb_repeat_t repeat)
{
	int i;
	kdbtab_t *kp;

	/*
	 *  Brute force method to determine duplicates
	 */
	for_each_kdbcmd(kp, i) {
		if (kp->cmd_name && (strcmp(kp->cmd_name, cmd) == 0)) {
			kdb_printf("Duplicate kdb command registered: "
				"%s, func %p help %s\n", cmd, func, help);
			return 1;
		}
	}

	/*
	 * Insert command into first available location in table
	 */
	for_each_kdbcmd(kp, i) {
		if (kp->cmd_name == NULL)
			break;
	}

	if (i >= kdb_max_commands) {
		kdbtab_t *new = kmalloc((kdb_max_commands - KDB_BASE_CMD_MAX +
			 kdb_command_extend) * sizeof(*new), GFP_KDB);
		if (!new) {
			kdb_printf("Could not allocate new kdb_command "
				   "table\n");
			return 1;
		}
		if (kdb_commands) {
			memcpy(new, kdb_commands,
			       kdb_max_commands * sizeof(*new));
			kfree(kdb_commands);
		}
		memset(new + kdb_max_commands, 0,
		       kdb_command_extend * sizeof(*new));
		kdb_commands = new;
		kp = kdb_commands + kdb_max_commands;
		kdb_max_commands += kdb_command_extend;
	}

	kp->cmd_name   = cmd;
	kp->cmd_func   = func;
	kp->cmd_usage  = usage;
	kp->cmd_help   = help;
	kp->cmd_flags  = 0;
	kp->cmd_minlen = minlen;
	kp->cmd_repeat = repeat;

	return 0;
}

/*
 * kdb_register - Compatibility register function for commands that do
 *	not need to specify a repeat state.  Equivalent to
 *	kdb_register_repeat with KDB_REPEAT_NONE.
 * Inputs:
 *	cmd	Command name
 *	func	Function to execute the command
 *	usage	A simple usage string showing arguments
 *	help	A simple help string describing command
 * Returns:
 *	zero for success, one if a duplicate command.
 */
int kdb_register(char *cmd,
	     kdb_func_t func,
	     char *usage,
	     char *help,
	     short minlen)
{
	return kdb_register_repeat(cmd, func, usage, help, minlen,
				   KDB_REPEAT_NONE);
}

/*
 * kdb_unregister - This function is used to unregister a kernel
 *	debugger command.  It is generally called when a module which
 *	implements kdb commands is unloaded.
 * Inputs:
 *	cmd	Command name
 * Returns:
 *	zero for success, one command not registered.
 */
int kdb_unregister(char *cmd)
{
	int i;
	kdbtab_t *kp;

	/*
	 *  find the command.
	 */
	for (i = 0, kp = kdb_commands; i < kdb_max_commands; i++, kp++) {
		if (kp->cmd_name && (strcmp(kp->cmd_name, cmd) == 0)) {
			kp->cmd_name = NULL;
			return 0;
		}
	}

	/* Couldn't find it.  */
	return 1;
}

/* Initialize the kdb command table. */
static void __init kdb_inittab(void)
{
	int i;
	kdbtab_t *kp;

	for_each_kdbcmd(kp, i)
		kp->cmd_name = NULL;

	kdb_register_repeat("md", kdb_md, "<vaddr>",
	  "Display Memory Contents, also mdWcN, e.g. md8c1", 1,
			    KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mdr", kdb_md, "<vaddr> <bytes>",
	  "Display Raw Memory", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mdp", kdb_md, "<paddr> <bytes>",
	  "Display Physical Memory", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mds", kdb_md, "<vaddr>",
	  "Display Memory Symbolically", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("mm", kdb_mm, "<vaddr> <contents>",
	  "Modify Memory Contents", 0, KDB_REPEAT_NO_ARGS);
	kdb_register_repeat("go", kdb_go, "[<vaddr>]",
	  "Continue Execution", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("rd", kdb_rd, "",
	  "Display Registers", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("rm", kdb_rm, "<reg> <contents>",
	  "Modify Registers", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("ef", kdb_ef, "<vaddr>",
	  "Display exception frame", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bt", kdb_bt, "[<vaddr>]",
	  "Stack traceback", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("btp", kdb_bt, "<pid>",
	  "Display stack for process <pid>", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("bta", kdb_bt, "[DRSTCZEUIMA]",
	  "Display stack all processes", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("btc", kdb_bt, "",
	  "Backtrace current process on each cpu", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("btt", kdb_bt, "<vaddr>",
	  "Backtrace process given its struct task address", 0,
			    KDB_REPEAT_NONE);
	kdb_register_repeat("ll", kdb_ll, "<first-element> <linkoffset> <cmd>",
	  "Execute cmd for each element in linked list", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("env", kdb_env, "",
	  "Show environment variables", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("set", kdb_set, "",
	  "Set environment variables", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("help", kdb_help, "",
	  "Display Help Message", 1, KDB_REPEAT_NONE);
	kdb_register_repeat("?", kdb_help, "",
	  "Display Help Message", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("cpu", kdb_cpu, "<cpunum>",
	  "Switch to new cpu", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("kgdb", kdb_kgdb, "",
	  "Enter kgdb mode", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("ps", kdb_ps, "[<flags>|A]",
	  "Display active task list", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("pid", kdb_pid, "<pidnum>",
	  "Switch to another task", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("reboot", kdb_reboot, "",
	  "Reboot the machine immediately", 0, KDB_REPEAT_NONE);
#if defined(CONFIG_MODULES)
	kdb_register_repeat("lsmod", kdb_lsmod, "",
	  "List loaded kernel modules", 0, KDB_REPEAT_NONE);
#endif
#if defined(CONFIG_MAGIC_SYSRQ)
	kdb_register_repeat("sr", kdb_sr, "<key>",
	  "Magic SysRq key", 0, KDB_REPEAT_NONE);
#endif
#if defined(CONFIG_PRINTK)
	kdb_register_repeat("dmesg", kdb_dmesg, "[lines]",
	  "Display syslog buffer", 0, KDB_REPEAT_NONE);
#endif
	kdb_register_repeat("defcmd", kdb_defcmd, "name \"usage\" \"help\"",
	  "Define a set of commands, down to endefcmd", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("kill", kdb_kill, "<-signal> <pid>",
	  "Send a signal to a process", 0, KDB_REPEAT_NONE);
	kdb_register_repeat("summary", kdb_summary, "",
	  "Summarize the system", 4, KDB_REPEAT_NONE);
	kdb_register_repeat("per_cpu", kdb_per_cpu, "",
	  "Display per_cpu variables", 3, KDB_REPEAT_NONE);
	kdb_register_repeat("grephelp", kdb_grep_help, "",
	  "Display help on | grep", 0, KDB_REPEAT_NONE);
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

/* Intialize kdb_printf, breakpoint tables and kdb state */
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
