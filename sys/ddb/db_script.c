/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Robert N. M. Watson
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
 */

/*-
 * Simple DDB scripting mechanism.  Each script consists of a named list of
 * DDB commands to execute sequentially.  A more sophisticated scripting
 * language might be desirable, but would be significantly more complex to
 * implement.  A more interesting syntax might allow general use of variables
 * and extracting of useful values, such as a thread's process identifier,
 * for passing into further DDB commands.  Certain scripts are run
 * automatically at kdb_enter(), if defined, based on how the debugger is
 * entered, allowing scripted responses to panics, break signals, etc.
 *
 * Scripts may be managed from within DDB using the script, scripts, and
 * unscript commands.  They may also be managed from userspace using ddb(8),
 * which operates using a set of sysctls.
 *
 * TODO:
 * - Allow scripts to be defined using tunables so that they can be defined
 *   before boot and be present in single-user mode without boot scripts
 *   running.
 * - Memory allocation is not possible from within DDB, so we use a set of
 *   statically allocated buffers to hold defined scripts.  However, when
 *   scripts are being defined from userspace via sysctl, we could in fact be
 *   using malloc(9) and therefore not impose a static limit, giving greater
 *   flexibility and avoiding hard-defined buffer limits.
 * - When scripts run automatically on entrance to DDB, placing "continue" at
 *   the end still results in being in the debugger, as we unconditionally
 *   run db_command_loop() after the script.  There should be a way to avoid
 *   this.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <ddb/ddb.h>
#include <ddb/db_command.h>
#include <ddb/db_lex.h>

#include <machine/setjmp.h>

/*
 * struct ddb_script describes an individual script.
 */
struct ddb_script {
	char	ds_scriptname[DB_MAXSCRIPTNAME];
	char	ds_script[DB_MAXSCRIPTLEN];
};

/*
 * Global list of scripts -- defined scripts have non-empty name fields.
 */
static struct ddb_script	db_script_table[DB_MAXSCRIPTS];

/*
 * While executing a script, we parse it using strsep(), so require a
 * temporary buffer that may be used destructively.  Since we support weak
 * recursion of scripts (one may reference another), we need one buffer for
 * each concurrently executing script.
 */
static struct db_recursion_data {
	char	drd_buffer[DB_MAXSCRIPTLEN];
} db_recursion_data[DB_MAXSCRIPTRECURSION];
static int	db_recursion = -1;

/*
 * We use a separate static buffer for script validation so that it is safe
 * to validate scripts from within a script.  This is used only in
 * db_script_valid(), which should never be called reentrantly.
 */
static char	db_static_buffer[DB_MAXSCRIPTLEN];

/*
 * Synchronization is not required from within the debugger, as it is
 * singe-threaded (although reentrance must be carefully considered).
 * However, it is required when interacting with scripts from user space
 * processes.  Sysctl procedures acquire db_script_mtx before accessing the
 * global script data structures.
 */
static struct mtx 	db_script_mtx;
MTX_SYSINIT(db_script_mtx, &db_script_mtx, "db_script_mtx", MTX_DEF);

/*
 * Some script names have special meaning, such as those executed
 * automatically when KDB is entered.
 */
#define	DB_SCRIPT_KDBENTER_PREFIX	"kdb.enter"	/* KDB has entered. */
#define	DB_SCRIPT_KDBENTER_DEFAULT	"kdb.enter.default"

/*
 * Find the existing script slot for a named script, if any.
 */
static struct ddb_script *
db_script_lookup(const char *scriptname)
{
	int i;

	for (i = 0; i < DB_MAXSCRIPTS; i++) {
		if (strcmp(db_script_table[i].ds_scriptname, scriptname) ==
		    0)
			return (&db_script_table[i]);
	}
	return (NULL);
}

/*
 * Find a new slot for a script, if available.  Does not mark as allocated in
 * any way--this must be done by the caller.
 */
static struct ddb_script *
db_script_new(void)
{
	int i;

	for (i = 0; i < DB_MAXSCRIPTS; i++) {
		if (strlen(db_script_table[i].ds_scriptname) == 0)
			return (&db_script_table[i]);
	}
	return (NULL);
}

/*
 * Perform very rudimentary validation of a proposed script.  It would be
 * easy to imagine something more comprehensive.  The script string is
 * validated in a static buffer.
 */
static int
db_script_valid(const char *scriptname, const char *script)
{
	char *buffer, *command;

	if (strlen(scriptname) == 0)
		return (EINVAL);
	if (strlen(scriptname) >= DB_MAXSCRIPTNAME)
		return (EINVAL);
	if (strlen(script) >= DB_MAXSCRIPTLEN)
		return (EINVAL);
	buffer = db_static_buffer;
	strcpy(buffer, script);
	while ((command = strsep(&buffer, ";")) != NULL) {
		if (strlen(command) >= DB_MAXLINE)
			return (EINVAL);
	}
	return (0);
}

/*
 * Modify an existing script or add a new script with the specified script
 * name and contents.  If there are no script slots available, an error will
 * be returned.
 */
static int
db_script_set(const char *scriptname, const char *script)
{
	struct ddb_script *dsp;
	int error;

	error = db_script_valid(scriptname, script);
	if (error)
		return (error);
	dsp = db_script_lookup(scriptname);
	if (dsp == NULL) {
		dsp = db_script_new();
		if (dsp == NULL)
			return (ENOSPC);
		strlcpy(dsp->ds_scriptname, scriptname,
		    sizeof(dsp->ds_scriptname));
	}
	strlcpy(dsp->ds_script, script, sizeof(dsp->ds_script));
	return (0);
}

/*
 * Delete an existing script by name, if found.
 */
static int
db_script_unset(const char *scriptname)
{
	struct ddb_script *dsp;

	dsp = db_script_lookup(scriptname);
	if (dsp == NULL)
		return (ENOENT);
	strcpy(dsp->ds_scriptname, "");
	strcpy(dsp->ds_script, "");
	return (0);
}

/*
 * Trim leading/trailing white space in a command so that we don't pass
 * carriage returns, etc, into DDB command parser.
 */
static int
db_command_trimmable(char ch)
{

	switch (ch) {
	case ' ':
	case '\t':
	case '\n':
	case '\r':
		return (1);

	default:
		return (0);
	}
}

static void
db_command_trim(char **commandp)
{
	char *command;

	command = *commandp;
	while (db_command_trimmable(*command))
		command++;
	while ((strlen(command) > 0) &&
	    db_command_trimmable(command[strlen(command) - 1]))
		command[strlen(command) - 1] = 0;
	*commandp = command;
}

/*
 * Execute a script, breaking it up into individual commands and passing them
 * sequentially into DDB's input processing.  Use the KDB jump buffer to
 * restore control to the main script loop if things get too wonky when
 * processing a command -- i.e., traps, etc.  Also, make sure we don't exceed
 * practical limits on recursion.
 *
 * XXXRW: If any individual command is too long, it will be truncated when
 * injected into the input at a lower layer.  We should validate the script
 * before configuring it to avoid this scenario.
 */
static int
db_script_exec(const char *scriptname, int warnifnotfound)
{
	struct db_recursion_data *drd;
	struct ddb_script *dsp;
	char *buffer, *command;
	void *prev_jb;
	jmp_buf jb;

	dsp = db_script_lookup(scriptname);
	if (dsp == NULL) {
		if (warnifnotfound)
			db_printf("script '%s' not found\n", scriptname);
		return (ENOENT);
	}

	if (db_recursion >= DB_MAXSCRIPTRECURSION) {
		db_printf("Script stack too deep\n");
		return (E2BIG);
	}
	db_recursion++;
	drd = &db_recursion_data[db_recursion];

	/*
	 * Parse script in temporary buffer, since strsep() is destructive.
	 */
	buffer = drd->drd_buffer;
	strcpy(buffer, dsp->ds_script);
	while ((command = strsep(&buffer, ";")) != NULL) {
		db_printf("db:%d:%s> %s\n", db_recursion, dsp->ds_scriptname,
		    command);
		db_command_trim(&command);
		prev_jb = kdb_jmpbuf(jb);
		if (setjmp(jb) == 0)
			db_command_script(command);
		else
			db_printf("Script command '%s' returned error\n",
			    command);
		kdb_jmpbuf(prev_jb);
	}
	db_recursion--;
	return (0);
}

/*
 * Wrapper for exec path that is called on KDB enter.  Map reason for KDB
 * enter to a script name, and don't whine if the script doesn't exist.  If
 * there is no matching script, try the catch-all script.
 */
void
db_script_kdbenter(const char *eventname)
{
	char scriptname[DB_MAXSCRIPTNAME];

	snprintf(scriptname, sizeof(scriptname), "%s.%s",
	    DB_SCRIPT_KDBENTER_PREFIX, eventname);
	if (db_script_exec(scriptname, 0) == ENOENT)
		(void)db_script_exec(DB_SCRIPT_KDBENTER_DEFAULT, 0);
}

/*-
 * DDB commands for scripting, as reached via the DDB user interface:
 *
 * scripts				- lists scripts
 * run <scriptname>			- run a script
 * script <scriptname>			- prints script
 * script <scriptname> <script>		- set a script
 * unscript <scriptname>		- remove a script
 */

/*
 * List scripts and their contents.
 */
void
db_scripts_cmd(db_expr_t addr, bool have_addr, db_expr_t count,
    char *modif)
{
	int i;

	for (i = 0; i < DB_MAXSCRIPTS; i++) {
		if (strlen(db_script_table[i].ds_scriptname) != 0) {
			db_printf("%s=%s\n",
			    db_script_table[i].ds_scriptname,
			    db_script_table[i].ds_script);
		}
	}
}

/*
 * Execute a script.
 */
void
db_run_cmd(db_expr_t addr, bool have_addr, db_expr_t count, char *modif)
{
	int t;

	/*
	 * Right now, we accept exactly one argument.  In the future, we
	 * might want to accept flags and arguments to the script itself.
	 */
	t = db_read_token();
	if (t != tIDENT)
		db_error("?\n");

	if (db_read_token() != tEOL)
		db_error("?\n");

	db_script_exec(db_tok_string, 1);
}

/*
 * Print or set a named script, with the set portion broken out into its own
 * function.  We must directly access the remainder of the DDB line input as
 * we do not wish to use db_lex's token processing.
 */
void
db_script_cmd(db_expr_t addr, bool have_addr, db_expr_t count,
    char *modif)
{
	char *buf, scriptname[DB_MAXSCRIPTNAME];
	struct ddb_script *dsp;
	int error, t;

	t = db_read_token();
	if (t != tIDENT) {
		db_printf("usage: script scriptname=script\n");
		db_skip_to_eol();
		return;
	}

	if (strlcpy(scriptname, db_tok_string, sizeof(scriptname)) >=
	    sizeof(scriptname)) {
		db_printf("scriptname too long\n");
		db_skip_to_eol();
		return;
	}

	t = db_read_token();
	if (t == tEOL) {
		dsp = db_script_lookup(scriptname);
		if (dsp == NULL) {
			db_printf("script '%s' not found\n", scriptname);
			db_skip_to_eol();
			return;
		}
		db_printf("%s=%s\n", scriptname, dsp->ds_script);
	} else if (t == tEQ) {
		buf = db_get_line();
		if (buf[strlen(buf)-1] == '\n')
			buf[strlen(buf)-1] = '\0';
		error = db_script_set(scriptname, buf);
		if (error != 0)
			db_printf("Error: %d\n", error);
	} else
		db_printf("?\n");
	db_skip_to_eol();
}

/*
 * Remove a named script.
 */
void
db_unscript_cmd(db_expr_t addr, bool have_addr, db_expr_t count,
    char *modif)
{
	int error, t;

	t = db_read_token();
	if (t != tIDENT) {
		db_printf("?\n");
		db_skip_to_eol();
		return;
	}

	error = db_script_unset(db_tok_string);
	if (error == ENOENT) {
		db_printf("script '%s' not found\n", db_tok_string);
		db_skip_to_eol();
		return;
	}
	db_skip_to_eol();
}

/*
 * Sysctls for managing DDB scripting:
 *
 * debug.ddb.scripting.script      - Define a new script
 * debug.ddb.scripting.scripts     - List of names *and* scripts
 * debug.ddb.scripting.unscript    - Remove an existing script
 *
 * Since we don't want to try to manage arbitrary extensions to the sysctl
 * name space from the debugger, the script/unscript sysctls are a bit more
 * like RPCs and a bit less like normal get/set requests.  The ddb(8) command
 * line tool wraps them to make things a bit more user-friendly.
 */
static SYSCTL_NODE(_debug_ddb, OID_AUTO, scripting, CTLFLAG_RW, 0,
    "DDB script settings");

static int
sysctl_debug_ddb_scripting_scripts(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	int error, i, len;
	char *buffer;

	/*
	 * Make space to include a maximum-length name, = symbol,
	 * maximum-length script, and carriage return for every script that
	 * may be defined.
	 */
	len = DB_MAXSCRIPTS * (DB_MAXSCRIPTNAME + 1 + DB_MAXSCRIPTLEN + 1);
	buffer = malloc(len, M_TEMP, M_WAITOK);
	(void)sbuf_new(&sb, buffer, len, SBUF_FIXEDLEN);
	mtx_lock(&db_script_mtx);
	for (i = 0; i < DB_MAXSCRIPTS; i++) {
		if (strlen(db_script_table[i].ds_scriptname) == 0)
			continue;
		(void)sbuf_printf(&sb, "%s=%s\n",
		    db_script_table[i].ds_scriptname,
		    db_script_table[i].ds_script);
	}
	mtx_unlock(&db_script_mtx);
	sbuf_finish(&sb);
	error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb) + 1);
	sbuf_delete(&sb);
	free(buffer, M_TEMP);
	return (error);
}
SYSCTL_PROC(_debug_ddb_scripting, OID_AUTO, scripts, CTLTYPE_STRING |
    CTLFLAG_RD, 0, 0, sysctl_debug_ddb_scripting_scripts, "A",
    "List of defined scripts");

static int
sysctl_debug_ddb_scripting_script(SYSCTL_HANDLER_ARGS)
{
	char *buffer, *script, *scriptname;
	int error, len;

	/*
	 * Maximum length for an input string is DB_MAXSCRIPTNAME + '='
	 * symbol + DB_MAXSCRIPT.
	 */
	len = DB_MAXSCRIPTNAME + DB_MAXSCRIPTLEN + 1;
	buffer = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
	error = sysctl_handle_string(oidp, buffer, len, req);
	if (error)
		goto out;

	/*
	 * Argument will be in form scriptname=script, so split into the
	 * scriptname and script.
	 */
	script = buffer;
	scriptname = strsep(&script, "=");
	if (script == NULL) {
		error = EINVAL;
		goto out;
	}
	mtx_lock(&db_script_mtx);
	error = db_script_set(scriptname, script);
	mtx_unlock(&db_script_mtx);
out:
	free(buffer, M_TEMP);
	return (error);
}
SYSCTL_PROC(_debug_ddb_scripting, OID_AUTO, script, CTLTYPE_STRING |
    CTLFLAG_RW, 0, 0, sysctl_debug_ddb_scripting_script, "A",
    "Set a script");

/*
 * debug.ddb.scripting.unscript has somewhat unusual sysctl semantics -- set
 * the name of the script that you want to delete.
 */
static int
sysctl_debug_ddb_scripting_unscript(SYSCTL_HANDLER_ARGS)
{
	char name[DB_MAXSCRIPTNAME];
	int error;

	bzero(name, sizeof(name));
	error = sysctl_handle_string(oidp, name, sizeof(name), req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (0);
	mtx_lock(&db_script_mtx);
	error = db_script_unset(name);
	mtx_unlock(&db_script_mtx);
	if (error == ENOENT)
		return (EINVAL);	/* Don't confuse sysctl consumers. */
	return (0);
}
SYSCTL_PROC(_debug_ddb_scripting, OID_AUTO, unscript, CTLTYPE_STRING |
    CTLFLAG_RW, 0, 0, sysctl_debug_ddb_scripting_unscript, "A",
    "Unset a script");
