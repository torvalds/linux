/*-
 * SPDX-License-Identifier: MIT-CMU
 *
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */
/*
 * Command dispatcher.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker_set.h>
#include <sys/lock.h>
#include <sys/kdb.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/cons.h>
#include <sys/conf.h>
#include <sys/watchdog.h>
#include <sys/kernel.h>

#include <ddb/ddb.h>
#include <ddb/db_command.h>
#include <ddb/db_lex.h>
#include <ddb/db_output.h>

#include <machine/cpu.h>
#include <machine/setjmp.h>

/*
 * Exported global variables
 */
int		db_cmd_loop_done;
db_addr_t	db_dot;
db_addr_t	db_last_addr;
db_addr_t	db_prev;
db_addr_t	db_next;

static db_cmdfcn_t	db_dump;
static db_cmdfcn_t	db_fncall;
static db_cmdfcn_t	db_gdb;
static db_cmdfcn_t	db_halt;
static db_cmdfcn_t	db_kill;
static db_cmdfcn_t	db_reset;
static db_cmdfcn_t	db_stack_trace;
static db_cmdfcn_t	db_stack_trace_active;
static db_cmdfcn_t	db_stack_trace_all;
static db_cmdfcn_t	db_watchdog;

/*
 * 'show' commands
 */

static struct command db_show_active_cmds[] = {
	{ "trace",	db_stack_trace_active,	0,	NULL },
};
struct command_table db_show_active_table =
    LIST_HEAD_INITIALIZER(db_show_active_table);

static struct command db_show_all_cmds[] = {
	{ "trace",	db_stack_trace_all,	0,	NULL },
};
struct command_table db_show_all_table =
    LIST_HEAD_INITIALIZER(db_show_all_table);

static struct command db_show_cmds[] = {
	{ "active",	0,			0,	&db_show_active_table },
	{ "all",	0,			0,	&db_show_all_table },
	{ "registers",	db_show_regs,		0,	NULL },
	{ "breaks",	db_listbreak_cmd, 	0,	NULL },
	{ "threads",	db_show_threads,	0,	NULL },
};
struct command_table db_show_table = LIST_HEAD_INITIALIZER(db_show_table);

static struct command db_cmds[] = {
	{ "print",	db_print_cmd,		0,	NULL },
	{ "p",		db_print_cmd,		0,	NULL },
	{ "examine",	db_examine_cmd,		CS_SET_DOT, NULL },
	{ "x",		db_examine_cmd,		CS_SET_DOT, NULL },
	{ "search",	db_search_cmd,		CS_OWN|CS_SET_DOT, NULL },
	{ "set",	db_set_cmd,		CS_OWN,	NULL },
	{ "write",	db_write_cmd,		CS_MORE|CS_SET_DOT, NULL },
	{ "w",		db_write_cmd,		CS_MORE|CS_SET_DOT, NULL },
	{ "delete",	db_delete_cmd,		0,	NULL },
	{ "d",		db_delete_cmd,		0,	NULL },
	{ "dump",	db_dump,		0,	NULL },
	{ "break",	db_breakpoint_cmd,	0,	NULL },
	{ "b",		db_breakpoint_cmd,	0,	NULL },
	{ "dwatch",	db_deletewatch_cmd,	0,	NULL },
	{ "watch",	db_watchpoint_cmd,	CS_MORE,NULL },
	{ "dhwatch",	db_deletehwatch_cmd,	0,      NULL },
	{ "hwatch",	db_hwatchpoint_cmd,	0,      NULL },
	{ "step",	db_single_step_cmd,	0,	NULL },
	{ "s",		db_single_step_cmd,	0,	NULL },
	{ "continue",	db_continue_cmd,	0,	NULL },
	{ "c",		db_continue_cmd,	0,	NULL },
	{ "until",	db_trace_until_call_cmd,0,	NULL },
	{ "next",	db_trace_until_matching_cmd,0,	NULL },
	{ "match",	db_trace_until_matching_cmd,0,	NULL },
	{ "trace",	db_stack_trace,		CS_OWN,	NULL },
	{ "t",		db_stack_trace,		CS_OWN,	NULL },
	/* XXX alias for active trace */
	{ "acttrace",	db_stack_trace_active,	0,	NULL },
	/* XXX alias for all trace */
	{ "alltrace",	db_stack_trace_all,	0,	NULL },
	{ "where",	db_stack_trace,		CS_OWN,	NULL },
	{ "bt",		db_stack_trace,		CS_OWN,	NULL },
	{ "call",	db_fncall,		CS_OWN,	NULL },
	{ "show",	0,			0,	&db_show_table },
	{ "ps",		db_ps,			0,	NULL },
	{ "gdb",	db_gdb,			0,	NULL },
	{ "halt",	db_halt,		0,	NULL },
	{ "reboot",	db_reset,		0,	NULL },
	{ "reset",	db_reset,		0,	NULL },
	{ "kill",	db_kill,		CS_OWN,	NULL },
	{ "watchdog",	db_watchdog,		CS_OWN,	NULL },
	{ "thread",	db_set_thread,		0,	NULL },
	{ "run",	db_run_cmd,		CS_OWN,	NULL },
	{ "script",	db_script_cmd,		CS_OWN,	NULL },
	{ "scripts",	db_scripts_cmd,		0,	NULL },
	{ "unscript",	db_unscript_cmd,	CS_OWN,	NULL },
	{ "capture",	db_capture_cmd,		CS_OWN,	NULL },
	{ "textdump",	db_textdump_cmd,	CS_OWN, NULL },
	{ "findstack",	db_findstack_cmd,	0,	NULL },
};
struct command_table db_cmd_table = LIST_HEAD_INITIALIZER(db_cmd_table);

static struct command	*db_last_command = NULL;

/*
 * if 'ed' style: 'dot' is set at start of last item printed,
 * and '+' points to next line.
 * Otherwise: 'dot' points to next item, '..' points to last.
 */
static bool	db_ed_style = true;

/*
 * Utility routine - discard tokens through end-of-line.
 */
void
db_skip_to_eol(void)
{
	int	t;
	do {
	    t = db_read_token();
	} while (t != tEOL);
}

/*
 * Results of command search.
 */
#define	CMD_UNIQUE	0
#define	CMD_FOUND	1
#define	CMD_NONE	2
#define	CMD_AMBIGUOUS	3
#define	CMD_HELP	4

static void	db_cmd_match(char *name, struct command *cmd,
		    struct command **cmdp, int *resultp);
static void	db_cmd_list(struct command_table *table);
static int	db_cmd_search(char *name, struct command_table *table,
		    struct command **cmdp);
static void	db_command(struct command **last_cmdp,
		    struct command_table *cmd_table, int dopager);

/*
 * Initialize the command lists from the static tables.
 */
void
db_command_init(void)
{
#define	N(a)	(sizeof(a) / sizeof(a[0]))
	int i;

	for (i = 0; i < N(db_cmds); i++)
		db_command_register(&db_cmd_table, &db_cmds[i]);
	for (i = 0; i < N(db_show_cmds); i++)
		db_command_register(&db_show_table, &db_show_cmds[i]);
	for (i = 0; i < N(db_show_active_cmds); i++)
		db_command_register(&db_show_active_table,
		    &db_show_active_cmds[i]);
	for (i = 0; i < N(db_show_all_cmds); i++)
		db_command_register(&db_show_all_table, &db_show_all_cmds[i]);
#undef N
}

/*
 * Register a command.
 */
void
db_command_register(struct command_table *list, struct command *cmd)
{
	struct command *c, *last;

	last = NULL;
	LIST_FOREACH(c, list, next) {
		int n = strcmp(cmd->name, c->name);

		/* Check that the command is not already present. */
		if (n == 0) {
			printf("%s: Warning, the command \"%s\" already exists;"
			     " ignoring request\n", __func__, cmd->name);
			return;
		}
		if (n < 0) {
			/* NB: keep list sorted lexicographically */
			LIST_INSERT_BEFORE(c, cmd, next);
			return;
		}
		last = c;
	}
	if (last == NULL)
		LIST_INSERT_HEAD(list, cmd, next);
	else
		LIST_INSERT_AFTER(last, cmd, next);
}

/*
 * Remove a command previously registered with db_command_register.
 */
void
db_command_unregister(struct command_table *list, struct command *cmd)
{
	struct command *c;

	LIST_FOREACH(c, list, next) {
		if (cmd == c) {
			LIST_REMOVE(cmd, next);
			return;
		}
	}
	/* NB: intentionally quiet */
}

/*
 * Helper function to match a single command.
 */
static void
db_cmd_match(char *name, struct command *cmd, struct command **cmdp,
    int *resultp)
{
	char *lp, *rp;
	int c;

	lp = name;
	rp = cmd->name;
	while ((c = *lp) == *rp) {
		if (c == 0) {
			/* complete match */
			*cmdp = cmd;
			*resultp = CMD_UNIQUE;
			return;
		}
		lp++;
		rp++;
	}
	if (c == 0) {
		/* end of name, not end of command -
		   partial match */
		if (*resultp == CMD_FOUND) {
			*resultp = CMD_AMBIGUOUS;
			/* but keep looking for a full match -
			   this lets us match single letters */
		} else {
			*cmdp = cmd;
			*resultp = CMD_FOUND;
		}
	}
}

/*
 * Search for command prefix.
 */
static int
db_cmd_search(char *name, struct command_table *table, struct command **cmdp)
{
	struct command	*cmd;
	int		result = CMD_NONE;

	LIST_FOREACH(cmd, table, next) {
		db_cmd_match(name,cmd,cmdp,&result);
		if (result == CMD_UNIQUE)
			break;
	}

	if (result == CMD_NONE) {
		/* check for 'help' */
		if (name[0] == 'h' && name[1] == 'e'
		    && name[2] == 'l' && name[3] == 'p')
			result = CMD_HELP;
	}
	return (result);
}

static void
db_cmd_list(struct command_table *table)
{
	struct command	*cmd;
	int have_subcommands;

	have_subcommands = 0;
	LIST_FOREACH(cmd, table, next) {
		if (cmd->more != NULL)
			have_subcommands++;
		db_printf("%-16s", cmd->name);
		db_end_line(16);
	}

	if (have_subcommands > 0) {
		db_printf("\nThe following have subcommands; append \"help\" "
		    "to list (e.g. \"show help\"):\n");
		LIST_FOREACH(cmd, table, next) {
			if (cmd->more == NULL)
				continue;
			db_printf("%-16s", cmd->name);
			db_end_line(16);
		}
	}
}

static void
db_command(struct command **last_cmdp, struct command_table *cmd_table,
    int dopager)
{
	struct command	*cmd = NULL;
	int		t;
	char		modif[TOK_STRING_SIZE];
	db_expr_t	addr, count;
	bool		have_addr = false;
	int		result;

	t = db_read_token();
	if (t == tEOL) {
	    /* empty line repeats last command, at 'next' */
	    cmd = *last_cmdp;
	    addr = (db_expr_t)db_next;
	    have_addr = false;
	    count = 1;
	    modif[0] = '\0';
	}
	else if (t == tEXCL) {
	    db_fncall((db_expr_t)0, (bool)false, (db_expr_t)0, (char *)0);
	    return;
	}
	else if (t != tIDENT) {
	    db_printf("Unrecognized input; use \"help\" "
	        "to list available commands\n");
	    db_flush_lex();
	    return;
	}
	else {
	    /*
	     * Search for command
	     */
	    while (cmd_table) {
		result = db_cmd_search(db_tok_string,
				       cmd_table,
				       &cmd);
		switch (result) {
		    case CMD_NONE:
			db_printf("No such command; use \"help\" "
			    "to list available commands\n");
			db_flush_lex();
			return;
		    case CMD_AMBIGUOUS:
			db_printf("Ambiguous\n");
			db_flush_lex();
			return;
		    case CMD_HELP:
			if (cmd_table == &db_cmd_table) {
			    db_printf("This is ddb(4), the kernel debugger; "
			        "see https://man.FreeBSD.org/ddb/4 for help.\n");
			    db_printf("Use \"bt\" for backtrace, \"dump\" for "
			        "kernel core dump, \"reset\" to reboot.\n");
			    db_printf("Available commands:\n");
			}
			db_cmd_list(cmd_table);
			db_flush_lex();
			return;
		    default:
			break;
		}
		if ((cmd_table = cmd->more) != NULL) {
		    t = db_read_token();
		    if (t != tIDENT) {
			db_printf("Subcommand required; "
			    "available subcommands:\n");
			db_cmd_list(cmd_table);
			db_flush_lex();
			return;
		    }
		}
	    }

	    if ((cmd->flag & CS_OWN) == 0) {
		/*
		 * Standard syntax:
		 * command [/modifier] [addr] [,count]
		 */
		t = db_read_token();
		if (t == tSLASH) {
		    t = db_read_token();
		    if (t != tIDENT) {
			db_printf("Bad modifier\n");
			db_flush_lex();
			return;
		    }
		    db_strcpy(modif, db_tok_string);
		}
		else {
		    db_unread_token(t);
		    modif[0] = '\0';
		}

		if (db_expression(&addr)) {
		    db_dot = (db_addr_t) addr;
		    db_last_addr = db_dot;
		    have_addr = true;
		}
		else {
		    addr = (db_expr_t) db_dot;
		    have_addr = false;
		}
		t = db_read_token();
		if (t == tCOMMA) {
		    if (!db_expression(&count)) {
			db_printf("Count missing\n");
			db_flush_lex();
			return;
		    }
		}
		else {
		    db_unread_token(t);
		    count = -1;
		}
		if ((cmd->flag & CS_MORE) == 0) {
		    db_skip_to_eol();
		}
	    }
	}
	*last_cmdp = cmd;
	if (cmd != NULL) {
	    /*
	     * Execute the command.
	     */
	    if (dopager)
		db_enable_pager();
	    else
		db_disable_pager();
	    (*cmd->fcn)(addr, have_addr, count, modif);
	    if (dopager)
		db_disable_pager();

	    if (cmd->flag & CS_SET_DOT) {
		/*
		 * If command changes dot, set dot to
		 * previous address displayed (if 'ed' style).
		 */
		if (db_ed_style) {
		    db_dot = db_prev;
		}
		else {
		    db_dot = db_next;
		}
	    }
	    else {
		/*
		 * If command does not change dot,
		 * set 'next' location to be the same.
		 */
		db_next = db_dot;
	    }
	}
}

/*
 * At least one non-optional command must be implemented using
 * DB_COMMAND() so that db_cmd_set gets created.  Here is one.
 */
DB_COMMAND(panic, db_panic)
{
	db_disable_pager();
	panic("from debugger");
}

void
db_command_loop(void)
{
	/*
	 * Initialize 'prev' and 'next' to dot.
	 */
	db_prev = db_dot;
	db_next = db_dot;

	db_cmd_loop_done = 0;
	while (!db_cmd_loop_done) {
	    if (db_print_position() != 0)
		db_printf("\n");

	    db_printf("db> ");
	    (void) db_read_line();

	    db_command(&db_last_command, &db_cmd_table, /* dopager */ 1);
	}
}

/*
 * Execute a command on behalf of a script.  The caller is responsible for
 * making sure that the command string is < DB_MAXLINE or it will be
 * truncated.
 *
 * XXXRW: Runs by injecting faked input into DDB input stream; it would be
 * nicer to use an alternative approach that didn't mess with the previous
 * command buffer.
 */
void
db_command_script(const char *command)
{
	db_prev = db_next = db_dot;
	db_inject_line(command);
	db_command(&db_last_command, &db_cmd_table, /* dopager */ 0);
}

void
db_error(const char *s)
{
	if (s)
	    db_printf("%s", s);
	db_flush_lex();
	kdb_reenter_silent();
}

static void
db_dump(db_expr_t dummy, bool dummy2, db_expr_t dummy3, char *dummy4)
{
	int error;

	if (textdump_pending) {
		db_printf("textdump_pending set.\n"
		    "run \"textdump unset\" first or \"textdump dump\" for a textdump.\n");
		return;
	}
	error = doadump(false);
	if (error) {
		db_printf("Cannot dump: ");
		switch (error) {
		case EBUSY:
			db_printf("debugger got invoked while dumping.\n");
			break;
		case ENXIO:
			db_printf("no dump device specified.\n");
			break;
		default:
			db_printf("unknown error (error=%d).\n", error);
			break;
		}
	}
}

/*
 * Call random function:
 * !expr(arg,arg,arg)
 */

/* The generic implementation supports a maximum of 10 arguments. */
typedef db_expr_t __db_f(db_expr_t, db_expr_t, db_expr_t, db_expr_t,
    db_expr_t, db_expr_t, db_expr_t, db_expr_t, db_expr_t, db_expr_t);

static __inline int
db_fncall_generic(db_expr_t addr, db_expr_t *rv, int nargs, db_expr_t args[])
{
	__db_f *f = (__db_f *)addr;

	if (nargs > 10) {
		db_printf("Too many arguments (max 10)\n");
		return (0);
	}
	*rv = (*f)(args[0], args[1], args[2], args[3], args[4], args[5],
	    args[6], args[7], args[8], args[9]);
	return (1);
}

static void
db_fncall(db_expr_t dummy1, bool dummy2, db_expr_t dummy3, char *dummy4)
{
	db_expr_t	fn_addr;
	db_expr_t	args[DB_MAXARGS];
	int		nargs = 0;
	db_expr_t	retval;
	int		t;

	if (!db_expression(&fn_addr)) {
	    db_printf("Bad function\n");
	    db_flush_lex();
	    return;
	}

	t = db_read_token();
	if (t == tLPAREN) {
	    if (db_expression(&args[0])) {
		nargs++;
		while ((t = db_read_token()) == tCOMMA) {
		    if (nargs == DB_MAXARGS) {
			db_printf("Too many arguments (max %d)\n", DB_MAXARGS);
			db_flush_lex();
			return;
		    }
		    if (!db_expression(&args[nargs])) {
			db_printf("Argument missing\n");
			db_flush_lex();
			return;
		    }
		    nargs++;
		}
		db_unread_token(t);
	    }
	    if (db_read_token() != tRPAREN) {
	        db_printf("Mismatched parens\n");
		db_flush_lex();
		return;
	    }
	}
	db_skip_to_eol();
	db_disable_pager();

	if (DB_CALL(fn_addr, &retval, nargs, args))
		db_printf("= %#lr\n", (long)retval);
}

static void
db_halt(db_expr_t dummy, bool dummy2, db_expr_t dummy3, char *dummy4)
{

	cpu_halt();
}

static void
db_kill(db_expr_t dummy1, bool dummy2, db_expr_t dummy3, char *dummy4)
{
	db_expr_t old_radix, pid, sig;
	struct proc *p;

#define	DB_ERROR(f)	do { db_printf f; db_flush_lex(); goto out; } while (0)

	/*
	 * PIDs and signal numbers are typically represented in base
	 * 10, so make that the default here.  It can, of course, be
	 * overridden by specifying a prefix.
	 */
	old_radix = db_radix;
	db_radix = 10;
	/* Retrieve arguments. */
	if (!db_expression(&sig))
		DB_ERROR(("Missing signal number\n"));
	if (!db_expression(&pid))
		DB_ERROR(("Missing process ID\n"));
	db_skip_to_eol();
	if (!_SIG_VALID(sig))
		DB_ERROR(("Signal number out of range\n"));

	/*
	 * Find the process in question.  allproc_lock is not needed
	 * since we're in DDB.
	 */
	/* sx_slock(&allproc_lock); */
	FOREACH_PROC_IN_SYSTEM(p)
	    if (p->p_pid == pid)
		    break;
	/* sx_sunlock(&allproc_lock); */
	if (p == NULL)
		DB_ERROR(("Can't find process with pid %ld\n", (long) pid));

	/* If it's already locked, bail; otherwise, do the deed. */
	if (PROC_TRYLOCK(p) == 0)
		DB_ERROR(("Can't lock process with pid %ld\n", (long) pid));
	else {
		pksignal(p, sig, NULL);
		PROC_UNLOCK(p);
	}

out:
	db_radix = old_radix;
#undef DB_ERROR
}

/*
 * Reboot.  In case there is an additional argument, take it as delay in
 * seconds.  Default to 15s if we cannot parse it and make sure we will
 * never wait longer than 1 week.  Some code is similar to
 * kern_shutdown.c:shutdown_panic().
 */
#ifndef	DB_RESET_MAXDELAY
#define	DB_RESET_MAXDELAY	(3600 * 24 * 7)
#endif

static void
db_reset(db_expr_t addr, bool have_addr, db_expr_t count __unused,
    char *modif __unused)
{
	int delay, loop;

	if (have_addr) {
		delay = (int)db_hex2dec(addr);

		/* If we parse to fail, use 15s. */
		if (delay == -1)
			delay = 15;

		/* Cap at one week. */
		if ((uintmax_t)delay > (uintmax_t)DB_RESET_MAXDELAY)
			delay = DB_RESET_MAXDELAY;

		db_printf("Automatic reboot in %d seconds - "
		    "press a key on the console to abort\n", delay);
		for (loop = delay * 10; loop > 0; --loop) {
			DELAY(1000 * 100); /* 1/10th second */
			/* Did user type a key? */
			if (cncheckc() != -1)
				return;
		}
	}

	cpu_reset();
}

static void
db_watchdog(db_expr_t dummy1, bool dummy2, db_expr_t dummy3, char *dummy4)
{
	db_expr_t old_radix, tout;
	int err, i;

	old_radix = db_radix;
	db_radix = 10;
	err = db_expression(&tout);
	db_skip_to_eol();
	db_radix = old_radix;

	/* If no argument is provided the watchdog will just be disabled. */
	if (err == 0) {
		db_printf("No argument provided, disabling watchdog\n");
		tout = 0;
	} else if ((tout & WD_INTERVAL) == WD_TO_NEVER) {
		db_error("Out of range watchdog interval\n");
		return;
	}
	EVENTHANDLER_INVOKE(watchdog_list, tout, &i);
}

static void
db_gdb(db_expr_t dummy1, bool dummy2, db_expr_t dummy3, char *dummy4)
{

	if (kdb_dbbe_select("gdb") != 0) {
		db_printf("The remote GDB backend could not be selected.\n");
		return;
	}
	/*
	 * Mark that we are done in the debugger.  kdb_trap()
	 * should re-enter with the new backend.
	 */
	db_cmd_loop_done = 1;
	db_printf("(ctrl-c will return control to ddb)\n");
}

static void
db_stack_trace(db_expr_t tid, bool hastid, db_expr_t count, char *modif)
{
	struct thread *td;
	db_expr_t radix;
	pid_t pid;
	int t;

	/*
	 * We parse our own arguments. We don't like the default radix.
	 */
	radix = db_radix;
	db_radix = 10;
	hastid = db_expression(&tid);
	t = db_read_token();
	if (t == tCOMMA) {
		if (!db_expression(&count)) {
			db_printf("Count missing\n");
			db_flush_lex();
			db_radix = radix;
			return;
		}
	} else {
		db_unread_token(t);
		count = -1;
	}
	db_skip_to_eol();
	db_radix = radix;

	if (hastid) {
		td = kdb_thr_lookup((lwpid_t)tid);
		if (td == NULL)
			td = kdb_thr_from_pid((pid_t)tid);
		if (td == NULL) {
			db_printf("Thread %d not found\n", (int)tid);
			return;
		}
	} else
		td = kdb_thread;
	if (td->td_proc != NULL)
		pid = td->td_proc->p_pid;
	else
		pid = -1;
	db_printf("Tracing pid %d tid %ld td %p\n", pid, (long)td->td_tid, td);
	db_trace_thread(td, count);
}

static void
_db_stack_trace_all(bool active_only)
{
	struct proc *p;
	struct thread *td;
	jmp_buf jb;
	void *prev_jb;

	FOREACH_PROC_IN_SYSTEM(p) {
		prev_jb = kdb_jmpbuf(jb);
		if (setjmp(jb) == 0) {
			FOREACH_THREAD_IN_PROC(p, td) {
				if (td->td_state == TDS_RUNNING)
					db_printf("\nTracing command %s pid %d"
					    " tid %ld td %p (CPU %d)\n",
					    p->p_comm, p->p_pid,
					    (long)td->td_tid, td,
					    td->td_oncpu);
				else if (active_only)
					continue;
				else
					db_printf("\nTracing command %s pid %d"
					    " tid %ld td %p\n", p->p_comm,
					    p->p_pid, (long)td->td_tid, td);
				db_trace_thread(td, -1);
				if (db_pager_quit) {
					kdb_jmpbuf(prev_jb);
					return;
				}
			}
		}
		kdb_jmpbuf(prev_jb);
	}
}

static void
db_stack_trace_active(db_expr_t dummy, bool dummy2, db_expr_t dummy3,
    char *dummy4)
{

	_db_stack_trace_all(true);
}

static void
db_stack_trace_all(db_expr_t dummy, bool dummy2, db_expr_t dummy3,
    char *dummy4)
{

	_db_stack_trace_all(false);
}

/*
 * Take the parsed expression value from the command line that was parsed
 * as a hexadecimal value and convert it as if the expression was parsed
 * as a decimal value.  Returns -1 if the expression was not a valid
 * decimal value.
 */
db_expr_t
db_hex2dec(db_expr_t expr)
{
	uintptr_t x, y;
	db_expr_t val;

	y = 1;
	val = 0;
	x = expr;
	while (x != 0) {
		if (x % 16 > 9)
			return (-1);
		val += (x % 16) * (y);
		x >>= 4;
		y *= 10;
	}
	return (val);
}
