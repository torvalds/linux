/*	$OpenBSD: db_command.c,v 1.103 2024/11/07 16:02:29 miod Exp $	*/
/*	$NetBSD: db_command.c,v 1.20 1996/03/30 22:30:05 christos Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 * Command dispatcher.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/extent.h>
#include <sys/pool.h>
#include <sys/msgbuf.h>
#include <sys/malloc.h>
#include <sys/mount.h>

#include <uvm/uvm_extern.h>
#include <machine/db_machdep.h>		/* type definitions */

#include <ddb/db_access.h>
#include <ddb/db_lex.h>
#include <ddb/db_output.h>
#include <ddb/db_command.h>
#include <ddb/db_break.h>
#include <ddb/db_watch.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>
#include <ddb/db_var.h>
#include <ddb/db_variables.h>
#include <ddb/db_interface.h>
#include <ddb/db_extern.h>

#include <netinet/ip_ipsp.h>
#include <uvm/uvm_ddb.h>

/*
 * Exported global variables
 */
int		db_cmd_loop_done;
label_t		*db_recover;

/*
 * if 'ed' style: 'dot' is set at start of last item printed,
 * and '+' points to next line.
 * Otherwise: 'dot' points to next item, '..' points to last.
 */
int		db_ed_style = 1;

vaddr_t		db_dot;		/* current location */
vaddr_t		db_last_addr;	/* last explicit address typed */
vaddr_t		db_prev;	/* last address examined
				   or written */
vaddr_t		db_next;	/* next address to be examined
				   or written */

int	db_cmd_search(char *, const struct db_command *,
	    const struct db_command **);
void	db_cmd_list(const struct db_command *);
void	db_ctf_pprint_cmd(db_expr_t, int, db_expr_t,char *);
void	db_map_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_buf_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_malloc_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_mbuf_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_mount_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_show_all_mounts(db_expr_t, int, db_expr_t, char *);
void	db_show_all_vnodes(db_expr_t, int, db_expr_t, char *);
void	db_show_all_bufs(db_expr_t, int, db_expr_t, char *);
void	db_show_all_tdbs(db_expr_t, int, db_expr_t, char *);
void	db_object_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_page_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_extent_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_pool_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_proc_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_uvmexp_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_tdb_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_vnode_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_nfsreq_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_nfsnode_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_swap_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_help_cmd(db_expr_t, int, db_expr_t, char *);
void	db_fncall(db_expr_t, int, db_expr_t, char *);
void	db_boot_sync_cmd(db_expr_t, int, db_expr_t, char *);
void	db_boot_crash_cmd(db_expr_t, int, db_expr_t, char *);
void	db_boot_dump_cmd(db_expr_t, int, db_expr_t, char *);
void	db_boot_halt_cmd(db_expr_t, int, db_expr_t, char *);
void	db_boot_reboot_cmd(db_expr_t, int, db_expr_t, char *);
void	db_boot_poweroff_cmd(db_expr_t, int, db_expr_t, char *);
void	db_stack_trace_cmd(db_expr_t, int, db_expr_t, char *);
void	db_dmesg_cmd(db_expr_t, int, db_expr_t, char *);
void	db_show_panic_cmd(db_expr_t, int, db_expr_t, char *);
void	db_bcstats_print_cmd(db_expr_t, int, db_expr_t, char *);
void	db_ctf_show_struct(db_expr_t, int, db_expr_t, char *);
void	db_show_regs(db_expr_t, int, db_expr_t, char *);
void	db_write_cmd(db_expr_t, int, db_expr_t, char *);
void	db_witness_display(db_expr_t, int, db_expr_t, char *);
void	db_witness_list(db_expr_t, int, db_expr_t, char *);
void	db_witness_list_all(db_expr_t, int, db_expr_t, char *);


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

/*
 * Search for command prefix.
 */
int
db_cmd_search(char *name, const struct db_command *table,
    const struct db_command **cmdp)
{
	const struct db_command	*cmd;
	int			result = CMD_NONE;

	for (cmd = table; cmd->name != 0; cmd++) {
		char *lp = name, *rp = cmd->name;
		int  c;

		while ((c = *lp) == *rp) {
			if (c == 0) {
				/* complete match */
				*cmdp = cmd;
				return (CMD_UNIQUE);
			}
			lp++;
			rp++;
		}
		if (c == 0) {
			/* end of name, not end of command - partial match */
			if (result == CMD_FOUND) {
				result = CMD_AMBIGUOUS;
				/* but keep looking for a full match -
				   this lets us match single letters */
			} else {
				*cmdp = cmd;
				result = CMD_FOUND;
			}
		}
	}
	return (result);
}

void
db_cmd_list(const struct db_command *table)
{
	const struct db_command *cmd;

	for (cmd = table; cmd->name != 0; cmd++) {
		db_printf("%-12s", cmd->name);
		db_end_line(12);
	}
}

void
db_command(const struct db_command **last_cmdp,
    const struct db_command *cmd_table)
{
	const struct db_command *cmd;
	char		modif[TOK_STRING_SIZE];
	db_expr_t	addr, count;
	int		t, result, have_addr = 0;

	t = db_read_token();
	if (t == tEOL) {
		/* empty line repeats last command, at 'next' */
		cmd = *last_cmdp;
		addr = (db_expr_t)db_next;
		have_addr = 0;
		count = 1;
		modif[0] = '\0';
	} else if (t == tEXCL) {
		db_fncall(0, 0, 0, NULL);
		return;
	} else if (t != tIDENT) {
		db_printf("?\n");
		db_flush_lex();
		return;
	} else {
		/* Search for command */
		while (cmd_table) {
			result = db_cmd_search(db_tok_string,
			    cmd_table, &cmd);
			switch (result) {
			case CMD_NONE:
				db_printf("No such command\n");
				db_flush_lex();
				return;
			case CMD_AMBIGUOUS:
				db_printf("Ambiguous\n");
				db_flush_lex();
				return;
			default:
				break;
			}
			if ((cmd_table = cmd->more) != 0) {
				t = db_read_token();
				if (t != tIDENT) {
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
				db_strlcpy(modif, db_tok_string, sizeof(modif));
			} else {
				db_unread_token(t);
				modif[0] = '\0';
			}

			if (db_expression(&addr)) {
				db_dot = (vaddr_t) addr;
				db_last_addr = db_dot;
				have_addr = 1;
			} else {
				addr = (db_expr_t) db_dot;
				have_addr = 0;
			}
			t = db_read_token();
			if (t == tCOMMA) {
				if (!db_expression(&count)) {
					db_printf("Count missing\n");
					db_flush_lex();
					return;
				}
			} else {
				db_unread_token(t);
				count = -1;
			}
			if ((cmd->flag & CS_MORE) == 0)
				db_skip_to_eol();
		}
	}
	*last_cmdp = cmd;
	if (cmd != 0) {
		/* Execute the command. */
		(*cmd->fcn)(addr, have_addr, count, modif);

		if (cmd->flag & CS_SET_DOT) {
			/*
			 * If command changes dot, set dot to
			 * previous address displayed (if 'ed' style).
			 */
			if (db_ed_style)
				db_dot = db_prev;
			else
				db_dot = db_next;
		}
	} else {
		/*
		 * If command does not change dot,
		 * set 'next' location to be the same.
		 */
		db_next = db_dot;
	}
}

void
db_buf_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	vfs_buf_print((void *) addr, full, db_printf);
}

void
db_map_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	uvm_map_printit((struct vm_map *) addr, full, db_printf);
}

void
db_malloc_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	malloc_printit(db_printf);
}

void
db_mbuf_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if ((modif[0] == 'c' && modif[1] == 'p') ||
	    (modif[0] == 'p' && modif[1] == 'c'))
		m_print_packet((void *)addr, 1, db_printf);
	else if (modif[0] == 'c')
		m_print_chain((void *)addr, 0, db_printf);
	else if (modif[0] == 'p')
		m_print_packet((void *)addr, 0, db_printf);
	else
		m_print((void *)addr, db_printf);
}

void
db_socket_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	so_print((void *)addr, db_printf);
}

void
db_mount_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	vfs_mount_print((struct mount *) addr, full, db_printf);
}

void
db_show_all_mounts(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;
	struct mount *mp;

	if (modif[0] == 'f')
		full = 1;

	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		db_printf("mountpoint %p\n", mp);
		vfs_mount_print(mp, full, db_printf);
	}
}

extern struct pool vnode_pool;
void
db_show_all_vnodes(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	pool_walk(&vnode_pool, full, db_printf, vfs_vnode_print);
}

extern struct pool bufpool;
void
db_show_all_bufs(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	pool_walk(&bufpool, full, db_printf, vfs_buf_print);
}

#ifdef IPSEC
void
db_show_all_tdbs(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	pool_walk(&tdb_pool, full, db_printf, tdb_printit);
}
#endif

void
db_show_all_routes(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	u_int rtableid = 0;

	if (have_addr)
		rtableid = addr;
	if (count == -1)
		count = 1;

	while (count--) {
		if (modif[0] != 'I')
			db_show_rtable(AF_INET, rtableid);
		if (modif[0] != 'i')
			db_show_rtable(AF_INET6, rtableid);
		rtableid++;
	}
}

void
db_show_route(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_show_rtentry((void *)addr, NULL, -1);
}

void
db_object_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	uvm_object_printit((struct uvm_object *) addr, full, db_printf);
}

void
db_page_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	uvm_page_printit((struct vm_page *) addr, full, db_printf);
}

void
db_vnode_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	vfs_vnode_print((void *)addr, full, db_printf);
}

#ifdef NFSCLIENT
void
db_nfsreq_print_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	nfs_request_print((void *)addr, full, db_printf);
}

void
db_nfsnode_print_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	nfs_node_print((void *)addr, full, db_printf);
}
#endif

void
db_swap_print_cmd(db_expr_t addr, int have_addr, db_expr_t count,
    char *modif)
{
	swap_print_all(db_printf);
}

void
db_show_panic_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct cpu_info *ci;
	char *prefix;
	CPU_INFO_ITERATOR cii;
	int panicked = 0;

	CPU_INFO_FOREACH(cii, ci) {
		if (ci->ci_panicbuf[0] != '\0') {
			prefix = (panicstr == ci->ci_panicbuf) ? "*" : " ";
			db_printf("%scpu%d: %s\n",
			    prefix, CPU_INFO_UNIT(ci), ci->ci_panicbuf);
			panicked = 1;
		}
	}
	if (!panicked)
		db_printf("the kernel did not panic\n");	/* yet */
}

void
db_extent_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	extent_print_all();
}

void
db_pool_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	pool_printit((struct pool *)addr, modif, db_printf);
}

void
db_proc_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if (!have_addr)
		addr = (db_expr_t)curproc;
	if (modif[0] == 't') {
		addr = (db_expr_t)tfind((pid_t)addr);
		if (addr == 0) {
			db_printf("not found\n");
			return;
		}
	}

	proc_printit((struct proc *)addr, modif, db_printf);
}

#ifdef IPSEC
void
db_tdb_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	tdb_printit((void *)addr, full, db_printf);
}
#endif

void
db_uvmexp_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	uvmexp_print(db_printf);
}

void	bcstats_print(int (*)(const char *, ...));

void
db_bcstats_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	bcstats_print(db_printf);
}

/*
 * 'show' commands
 */

const struct db_command db_show_all_cmds[] = {
	{ "procs",	db_show_all_procs,	0, NULL },
	{ "callout",	db_show_callout,	0, NULL },
	{ "clockintr",	db_show_all_clockintr,	0, NULL },
	{ "pools",	db_show_all_pools,	0, NULL },
	{ "mounts",	db_show_all_mounts,	0, NULL },
	{ "vnodes",	db_show_all_vnodes,	0, NULL },
	{ "bufs",	db_show_all_bufs,	0, NULL },
	{ "routes",	db_show_all_routes,	0, NULL },
#ifdef NFSCLIENT
	{ "nfsreqs",	db_show_all_nfsreqs,	0, NULL },
	{ "nfsnodes",	db_show_all_nfsnodes,	0, NULL },
#endif
#ifdef IPSEC
	{ "tdbs",	db_show_all_tdbs,	0, NULL },
#endif
#ifdef WITNESS
	{ "locks",	db_witness_list_all,	0, NULL },
#endif
	{ NULL,		NULL,			0, NULL }
};

const struct db_command db_show_cmds[] = {
	{ "all",	NULL,			0,	db_show_all_cmds },
	{ "bcstats",	db_bcstats_print_cmd,	0,	NULL },
	{ "breaks",	db_listbreak_cmd,	0,	NULL },
	{ "buf",	db_buf_print_cmd,	0,	NULL },
	{ "extents",	db_extent_print_cmd,	0,	NULL },
#ifdef WITNESS
	{ "locks",	db_witness_list,	0,	NULL },
#endif
	{ "malloc",	db_malloc_print_cmd,	0,	NULL },
	{ "map",	db_map_print_cmd,	0,	NULL },
	{ "mbuf",	db_mbuf_print_cmd,	0,	NULL },
	{ "mount",	db_mount_print_cmd,	0,	NULL },
#ifdef NFSCLIENT
	{ "nfsreq",	db_nfsreq_print_cmd,	0,	NULL },
	{ "nfsnode",	db_nfsnode_print_cmd,	0,	NULL },
#endif
	{ "object",	db_object_print_cmd,	0,	NULL },
	{ "page",	db_page_print_cmd,	0,	NULL },
	{ "panic",	db_show_panic_cmd,	0,	NULL },
	{ "pool",	db_pool_print_cmd,	0,	NULL },
	{ "proc",	db_proc_print_cmd,	0,	NULL },
	{ "registers",	db_show_regs,		0,	NULL },
	{ "route",	db_show_route,		0,	NULL },
	{ "socket",	db_socket_print_cmd,	0,	NULL },
	{ "struct",	db_ctf_show_struct,	CS_OWN,	NULL },
	{ "swap",	db_swap_print_cmd,	0,	NULL },
#ifdef IPSEC
	{ "tdb",	db_tdb_print_cmd,	0,	NULL },
#endif
	{ "uvmexp",	db_uvmexp_print_cmd,	0,	NULL },
	{ "vnode",	db_vnode_print_cmd,	0,	NULL },
	{ "watches",	db_listwatch_cmd,	0,	NULL },
#ifdef WITNESS
	{ "witness",	db_witness_display,	0,	NULL },
#endif
	{ NULL,		NULL,			0,	NULL }
};

const struct db_command db_boot_cmds[] = {
	{ "sync",	db_boot_sync_cmd,	0,	0 },
	{ "crash",	db_boot_crash_cmd,	0,	0 },
	{ "dump",	db_boot_dump_cmd,	0,	0 },
	{ "halt",	db_boot_halt_cmd,	0,	0 },
	{ "reboot",	db_boot_reboot_cmd,	0,	0 },
	{ "poweroff",	db_boot_poweroff_cmd,	0,	0 },
	{ NULL, }
};

const struct db_command db_command_table[] = {
#ifdef DB_MACHINE_COMMANDS
  /* this must be the first entry, if it exists */
	{ "machine",	NULL,			0, db_machine_command_table },
#endif
	{ "kill",	db_kill_cmd,		0,		NULL },
	{ "print",	db_print_cmd,		0,		NULL },
	{ "p",		db_print_cmd,		0,		NULL },
	{ "pprint",	db_ctf_pprint_cmd,	CS_OWN,		NULL },
	{ "examine",	db_examine_cmd,		CS_SET_DOT,	NULL },
	{ "x",		db_examine_cmd,		CS_SET_DOT,	NULL },
	{ "search",	db_search_cmd,		CS_OWN|CS_SET_DOT, NULL },
	{ "set",	db_set_cmd,		CS_OWN,		NULL },
	{ "write",	db_write_cmd,		CS_MORE|CS_SET_DOT, NULL },
	{ "w",		db_write_cmd,		CS_MORE|CS_SET_DOT, NULL },
	{ "delete",	db_delete_cmd,		0,		NULL },
	{ "d",		db_delete_cmd,		0,		NULL },
	{ "break",	db_breakpoint_cmd,	0,		NULL },
	{ "dwatch",	db_deletewatch_cmd,	0,		NULL },
	{ "watch",	db_watchpoint_cmd,	CS_MORE,	NULL },
	{ "step",	db_single_step_cmd,	0,		NULL },
	{ "s",		db_single_step_cmd,	0,		NULL },
	{ "continue",	db_continue_cmd,	0,		NULL },
	{ "c",		db_continue_cmd,	0,		NULL },
	{ "until",	db_trace_until_call_cmd,0,		NULL },
	{ "next",	db_trace_until_matching_cmd,0,		NULL },
	{ "match",	db_trace_until_matching_cmd,0,		NULL },
	{ "trace",	db_stack_trace_cmd,	0,		NULL },
	{ "bt",		db_stack_trace_cmd,	0,		NULL },
	{ "call",	db_fncall,		CS_OWN,		NULL },
	{ "ps",		db_show_all_procs,	0,		NULL },
	{ "callout",	db_show_callout,	0,		NULL },
	{ "reboot",	db_boot_reboot_cmd,	0,		NULL },
	{ "show",	NULL,			0,		db_show_cmds },
	{ "boot",	NULL,			0,		db_boot_cmds },
	{ "help",	db_help_cmd,		0,		NULL },
	{ "hangman",	db_hangman,		0,		NULL },
	{ "dmesg",	db_dmesg_cmd,		0,		NULL },
	{ NULL,		NULL,			0,		NULL }
};

const struct db_command	*db_last_command = NULL;

void
db_help_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_cmd_list(db_command_table);
}

void
db_command_loop(void)
{
	label_t		db_jmpbuf;
	label_t		*savejmp;
	extern int	db_output_line;

	/*
	 * Initialize 'prev' and 'next' to dot.
	 */
	db_prev = db_dot;
	db_next = db_dot;

	db_cmd_loop_done = 0;

	savejmp = db_recover;
	db_recover = &db_jmpbuf;
	(void) setjmp(&db_jmpbuf);

	while (!db_cmd_loop_done) {

		if (db_print_position() != 0)
			db_printf("\n");
		db_output_line = 0;

#ifdef MULTIPROCESSOR
		db_printf("ddb{%d}> ", CPU_INFO_UNIT(curcpu()));
#else
		db_printf("ddb> ");
#endif
		(void) db_read_line();

		db_command(&db_last_command, db_command_table);
	}

	db_recover = savejmp;
}

void
db_error(char *s)
{
	if (s)
		db_printf("%s", s);
	db_flush_lex();
	if (db_recover != NULL)
		longjmp(db_recover);
}


/*
 * Call random function:
 * !expr(arg,arg,arg)
 */
void
db_fncall(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_expr_t	fn_addr;
#define	MAXARGS		11
	db_expr_t	args[MAXARGS];
	int		nargs = 0;
	db_expr_t	retval;
	db_expr_t	(*func)(db_expr_t, ...);
	int		t;
	char		tmpfmt[28];

	if (!db_expression(&fn_addr)) {
		db_printf("Bad function\n");
		db_flush_lex();
		return;
	}
	func = (db_expr_t (*)(db_expr_t, ...)) fn_addr;

	t = db_read_token();
	if (t == tLPAREN) {
		if (db_expression(&args[0])) {
			nargs++;
			while ((t = db_read_token()) == tCOMMA) {
				if (nargs == MAXARGS) {
					db_printf("Too many arguments\n");
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
			db_printf("?\n");
			db_flush_lex();
			return;
		}
	}
	db_skip_to_eol();

	while (nargs < MAXARGS)
		args[nargs++] = 0;

	retval = (*func)(args[0], args[1], args[2], args[3], args[4],
	    args[5], args[6], args[7], args[8], args[9]);
	db_printf("%s\n", db_format(tmpfmt, sizeof tmpfmt, retval,
	    DB_FORMAT_N, 1, 0));
}

void
db_reboot(int howto)
{
	spl0();
	if (!curproc)
		curproc = &proc0;
	reboot(howto);
}

void
db_boot_sync_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_reboot(RB_AUTOBOOT | RB_TIMEBAD | RB_USERREQ);
}

void
db_boot_crash_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_reboot(RB_NOSYNC | RB_DUMP | RB_TIMEBAD | RB_USERREQ);
}

void
db_boot_dump_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_reboot(RB_DUMP | RB_TIMEBAD | RB_USERREQ);
}

void
db_boot_halt_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_reboot(RB_NOSYNC | RB_HALT | RB_TIMEBAD | RB_USERREQ);
}

void
db_boot_reboot_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	boot(RB_RESET | RB_AUTOBOOT | RB_NOSYNC | RB_TIMEBAD | RB_USERREQ);
}

void
db_boot_poweroff_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	db_reboot(RB_NOSYNC | RB_HALT | RB_POWERDOWN | RB_TIMEBAD | RB_USERREQ);
}

void
db_dmesg_cmd(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	int i, off;
	char *p;

	if (!msgbufp || msgbufp->msg_magic != MSG_MAGIC)
		return;
	off = msgbufp->msg_bufx;
	if (off > msgbufp->msg_bufs)
		off = 0;
	for (i = 0, p = msgbufp->msg_bufc + off;
	    i < msgbufp->msg_bufs; i++, p++) {
		if (p >= msgbufp->msg_bufc + msgbufp->msg_bufs)
			p = msgbufp->msg_bufc;
		if (*p != '\0')
			db_putchar(*p);
	}
	db_putchar('\n');
}

void
db_stack_trace_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_stack_trace_print(addr, have_addr, count, modif, db_printf);
}

void
db_show_regs(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct db_variable *regp;
	db_expr_t	value, offset;
	const char *	name;
	char		tmpfmt[28];

	for (regp = db_regs; regp < db_eregs; regp++) {
		db_read_variable(regp, &value);
		db_printf("%-12s%s", regp->name,
		    db_format(tmpfmt, sizeof tmpfmt,
		    (long)value, DB_FORMAT_N, 1, sizeof(long) * 3));
		db_find_xtrn_sym_and_offset((vaddr_t)value, &name, &offset);
		if (name != 0 && offset <= db_maxoff && offset != value) {
			db_printf("\t%s", name);
			if (offset != 0)
				db_printf("+%s",
				    db_format(tmpfmt, sizeof tmpfmt,
				    (long)offset, DB_FORMAT_R, 1, 0));
		}
		db_printf("\n");
	}
	db_print_loc_and_inst(PC_REGS(&ddb_regs));
}

/*
 * Write to file.
 */
void
db_write_cmd(db_expr_t address, int have_addr, db_expr_t count, char *modif)
{
	vaddr_t		addr;
	db_expr_t	old_value;
	db_expr_t	new_value;
	int		size, wrote_one = 0;
	char		tmpfmt[28];

	addr = (vaddr_t) address;

	switch (modif[0]) {
	case 'b':
		size = 1;
		break;
	case 'h':
		size = 2;
		break;
	case 'l':
	case '\0':
		size = 4;
		break;
#ifdef __LP64__
	case 'q':
		size = 8;
		break;
#endif
	default:
		size = -1;
		db_error("Unknown size\n");
		/*NOTREACHED*/
	}

	while (db_expression(&new_value)) {
		old_value = db_get_value(addr, size, 0);
		db_printsym(addr, DB_STGY_ANY, db_printf);
		db_printf("\t\t%s\t", db_format(tmpfmt, sizeof tmpfmt,
		    old_value, DB_FORMAT_N, 0, 8));
		db_printf("=\t%s\n",  db_format(tmpfmt, sizeof tmpfmt,
		    new_value, DB_FORMAT_N, 0, 8));
		db_put_value(addr, size, new_value);
		addr += size;

		wrote_one = 1;
	}

	if (!wrote_one) {
		db_error("Nothing written.\n");
		/*NOTREACHED*/
	}

	db_next = addr;
	db_prev = addr - size;

	db_skip_to_eol();
}
