/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993, Garrett A. Wollman.
 * Copyright (c) 1993, University of Vermont and State Agricultural College.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Necessary declarations for the `ddb' kernel debugger.
 */

#ifndef _DDB_DDB_H_
#define	_DDB_DDB_H_

#ifdef SYSCTL_DECL
SYSCTL_DECL(_debug_ddb);
#endif

#include <machine/db_machdep.h>		/* type definitions */

#include <sys/queue.h>			/* LIST_* */
#include <sys/kernel.h>			/* SYSINIT */

#ifndef DB_MAXARGS
#define	DB_MAXARGS	10
#endif

#ifndef DB_MAXLINE
#define	DB_MAXLINE	120
#endif

#ifndef DB_MAXSCRIPTS
#define	DB_MAXSCRIPTS	8
#endif

#ifndef DB_MAXSCRIPTNAME
#define	DB_MAXSCRIPTNAME	32
#endif

#ifndef DB_MAXSCRIPTLEN
#define	DB_MAXSCRIPTLEN	128
#endif

#ifndef DB_MAXSCRIPTRECURSION
#define	DB_MAXSCRIPTRECURSION	3
#endif

#ifndef DB_STOFFS
#define DB_STOFFS(offs)		(offs)
#endif

#ifndef DB_CALL
#define	DB_CALL	db_fncall_generic
#else
int	DB_CALL(db_expr_t, db_expr_t *, int, db_expr_t[]);
#endif

/*
 * Extern variables to set the address and size of the symtab and strtab.
 * Most users should use db_fetch_symtab in order to set them from the
 * boot loader provided values.
 */
extern vm_offset_t ksymtab, kstrtab, ksymtab_size;

/*
 * There are three "command tables":
 * - One for simple commands; a list of these is displayed
 *   by typing 'help' at the debugger prompt.
 * - One for sub-commands of 'show'; to see this type 'show'
 *   without any arguments.
 * - The last one for sub-commands of 'show all'; type 'show all'
 *   without any argument to get a list.
 */
struct command;
LIST_HEAD(command_table, command);
extern struct command_table db_cmd_table;
extern struct command_table db_show_table;
extern struct command_table db_show_all_table;

/*
 * Type signature for a function implementing a ddb command.
 */
typedef void db_cmdfcn_t(db_expr_t addr, bool have_addr, db_expr_t count,
	    char *modif);

/*
 * Command table entry.
 */
struct command {
	char *	name;		/* command name */
	db_cmdfcn_t *fcn;	/* function to call */
	int	flag;		/* extra info: */
#define	CS_OWN		0x1	/* non-standard syntax */
#define	CS_MORE		0x2	/* standard syntax, but may have other words
				 * at end */
#define	CS_SET_DOT	0x100	/* set dot after command */
	struct command_table *more; /* another level of command */
	LIST_ENTRY(command) next; /* next entry in the command table */
};

/*
 * Arrange for the specified ddb command to be defined and
 * bound to the specified function.  Commands can be defined
 * in modules in which case they will be available only when
 * the module is loaded.
 */
#define	_DB_SET(_suffix, _name, _func, list, _flag, _more)	\
static struct command __CONCAT(_name,_suffix) = {		\
	.name	= __STRING(_name),				\
	.fcn	= _func,					\
	.flag	= _flag,					\
	.more	= _more						\
};								\
static void __CONCAT(__CONCAT(_name,_suffix),_add)(void *arg __unused) \
    { db_command_register(&list, &__CONCAT(_name,_suffix)); }	\
SYSINIT(__CONCAT(_name,_suffix), SI_SUB_KLD, SI_ORDER_ANY,	\
    __CONCAT(__CONCAT(_name,_suffix),_add), NULL);		\
static void __CONCAT(__CONCAT(_name,_suffix),_del)(void *arg __unused) \
    { db_command_unregister(&list, &__CONCAT(_name,_suffix)); }	\
SYSUNINIT(__CONCAT(_name,_suffix), SI_SUB_KLD, SI_ORDER_ANY,	\
    __CONCAT(__CONCAT(_name,_suffix),_del), NULL);

/*
 * Like _DB_SET but also create the function declaration which
 * must be followed immediately by the body; e.g.
 *   _DB_FUNC(_cmd, panic, db_panic, db_cmd_table, 0, NULL)
 *   {
 *	...panic implementation...
 *   }
 *
 * This macro is mostly used to define commands placed in one of
 * the ddb command tables; see DB_COMMAND, etc. below.
 */
#define	_DB_FUNC(_suffix, _name, _func, list, _flag, _more)	\
static db_cmdfcn_t _func;					\
_DB_SET(_suffix, _name, _func, list, _flag, _more);		\
static void							\
_func(db_expr_t addr, bool have_addr, db_expr_t count, char *modif)

/* common idom provided for backwards compatibility */
#define	DB_FUNC(_name, _func, list, _flag, _more)		\
	_DB_FUNC(_cmd, _name, _func, list, _flag, _more)

#define	DB_COMMAND(cmd_name, func_name) \
	_DB_FUNC(_cmd, cmd_name, func_name, db_cmd_table, 0, NULL)
#define	DB_ALIAS(alias_name, func_name) \
	_DB_SET(_cmd, alias_name, func_name, db_cmd_table, 0, NULL)
#define	DB_SHOW_COMMAND(cmd_name, func_name) \
	_DB_FUNC(_show, cmd_name, func_name, db_show_table, 0, NULL)
#define	DB_SHOW_ALIAS(alias_name, func_name) \
	_DB_SET(_show, alias_name, func_name, db_show_table, 0, NULL)
#define	DB_SHOW_ALL_COMMAND(cmd_name, func_name) \
	_DB_FUNC(_show_all, cmd_name, func_name, db_show_all_table, 0, NULL)
#define	DB_SHOW_ALL_ALIAS(alias_name, func_name) \
	_DB_SET(_show_all, alias_name, func_name, db_show_all_table, 0, NULL)

extern db_expr_t db_maxoff;
extern int db_indent;
extern int db_inst_count;
extern int db_load_count;
extern int db_store_count;
extern volatile int db_pager_quit;
extern db_expr_t db_radix;
extern db_expr_t db_max_width;
extern db_expr_t db_tab_stop_width;
extern db_expr_t db_lines_per_page;

struct thread;
struct vm_map;

void		db_check_interrupt(void);
void		db_clear_watchpoints(void);
db_addr_t	db_disasm(db_addr_t loc, bool altfmt);
				/* instruction disassembler */
void		db_error(const char *s);
int		db_expression(db_expr_t *valuep);
int		db_get_variable(db_expr_t *valuep);
void		db_iprintf(const char *,...) __printflike(1, 2);
struct proc	*db_lookup_proc(db_expr_t addr);
struct thread	*db_lookup_thread(db_expr_t addr, bool check_pid);
struct vm_map	*db_map_addr(vm_offset_t);
bool		db_map_current(struct vm_map *);
bool		db_map_equal(struct vm_map *, struct vm_map *);
int		db_md_set_watchpoint(db_expr_t addr, db_expr_t size);
int		db_md_clr_watchpoint(db_expr_t addr, db_expr_t size);
void		db_md_list_watchpoints(void);
void		db_print_loc_and_inst(db_addr_t loc);
void		db_print_thread(void);
int		db_printf(const char *fmt, ...) __printflike(1, 2);
int		db_read_bytes(vm_offset_t addr, size_t size, char *data);
				/* machine-dependent */
int		db_readline(char *lstart, int lsize);
void		db_restart_at_pc(bool watchpt);
int		db_set_variable(db_expr_t value);
void		db_set_watchpoints(void);
void		db_skip_to_eol(void);
bool		db_stop_at_pc(int type, int code, bool *is_breakpoint,
		    bool *is_watchpoint);
#define		db_strcpy	strcpy
void		db_trace_self(void);
int		db_trace_thread(struct thread *, int);
bool		db_value_of_name(const char *name, db_expr_t *valuep);
bool		db_value_of_name_pcpu(const char *name, db_expr_t *valuep);
bool		db_value_of_name_vnet(const char *name, db_expr_t *valuep);
int		db_write_bytes(vm_offset_t addr, size_t size, char *data);
void		db_command_register(struct command_table *, struct command *);
void		db_command_unregister(struct command_table *, struct command *);
int		db_fetch_ksymtab(vm_offset_t ksym_start, vm_offset_t ksym_end);

db_cmdfcn_t	db_breakpoint_cmd;
db_cmdfcn_t	db_capture_cmd;
db_cmdfcn_t	db_continue_cmd;
db_cmdfcn_t	db_delete_cmd;
db_cmdfcn_t	db_deletehwatch_cmd;
db_cmdfcn_t	db_deletewatch_cmd;
db_cmdfcn_t	db_examine_cmd;
db_cmdfcn_t	db_findstack_cmd;
db_cmdfcn_t	db_hwatchpoint_cmd;
db_cmdfcn_t	db_listbreak_cmd;
db_cmdfcn_t	db_scripts_cmd;
db_cmdfcn_t	db_print_cmd;
db_cmdfcn_t	db_ps;
db_cmdfcn_t	db_run_cmd;
db_cmdfcn_t	db_script_cmd;
db_cmdfcn_t	db_search_cmd;
db_cmdfcn_t	db_set_cmd;
db_cmdfcn_t	db_set_thread;
db_cmdfcn_t	db_show_regs;
db_cmdfcn_t	db_show_threads;
db_cmdfcn_t	db_single_step_cmd;
db_cmdfcn_t	db_textdump_cmd;
db_cmdfcn_t	db_trace_until_call_cmd;
db_cmdfcn_t	db_trace_until_matching_cmd;
db_cmdfcn_t	db_unscript_cmd;
db_cmdfcn_t	db_watchpoint_cmd;
db_cmdfcn_t	db_write_cmd;

/*
 * Interface between DDB and the DDB output capture facility.
 */
struct dumperinfo;
void	db_capture_dump(struct dumperinfo *di);
void	db_capture_enterpager(void);
void	db_capture_exitpager(void);
void	db_capture_write(char *buffer, u_int buflen);
void	db_capture_writech(char ch);

/*
 * Interface between DDB  and the script facility.
 */
void	db_script_kdbenter(const char *eventname);	/* KDB enter event. */

/*
 * Interface between DDB and the textdump facility.
 *
 * Text dump blocks are of a fixed size; textdump_block_buffer is a
 * statically allocated buffer that code interacting with textdumps can use
 * to prepare and hold a pending block in when calling writenextblock().
 */
#define	TEXTDUMP_BLOCKSIZE	512
extern char	textdump_block_buffer[TEXTDUMP_BLOCKSIZE];

void	textdump_mkustar(char *block_buffer, const char *filename,
	    u_int size);
void	textdump_restoreoff(off_t offset);
void	textdump_saveoff(off_t *offsetp);
int	textdump_writenextblock(struct dumperinfo *di, char *buffer);

/*
 * Interface between the kernel and textdumps.
 */
extern int	textdump_pending;	/* Call textdump_dumpsys() instead. */
void	textdump_dumpsys(struct dumperinfo *di);

#endif /* !_DDB_DDB_H_ */
