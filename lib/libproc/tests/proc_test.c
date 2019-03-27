/*-
 * Copyright (c) 2014-2017 Mark Johnston <markj@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/wait.h>

#include <libgen.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <atf-c.h>
#include <libelf.h>
#include <libproc.h>

static const char *aout_object = "a.out";
static const char *ldelf_object = "ld-elf.so.1";
static const char *target_prog_file = "target_prog";

/*
 * Run the test program. If the sig parameter is set to true, the test program
 * will deliver SIGUSR1 to itself during execution.
 */
static struct proc_handle *
start_prog(const struct atf_tc *tc, bool sig)
{
	char *argv[3];
	struct proc_handle *phdl;
	int error;

	asprintf(&argv[0], "%s/%s", atf_tc_get_config_var(tc, "srcdir"),
	    target_prog_file);
	ATF_REQUIRE(argv[0] != NULL);

	if (sig) {
		argv[1] = strdup("-s");
		argv[2] = NULL;
	} else {
		argv[1] = NULL;
	}

	error = proc_create(argv[0], argv, NULL, NULL, NULL, &phdl);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to run '%s'", target_prog_file);
	ATF_REQUIRE(phdl != NULL);

	free(argv[0]);
	free(argv[1]);

	return (phdl);
}

static void
set_bkpt(struct proc_handle *phdl, uintptr_t addr, u_long *saved)
{
	int error;

	error = proc_bkptset(phdl, addr, saved);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to set breakpoint at 0x%jx",
	    (uintmax_t)addr);
}

static void
remove_bkpt(struct proc_handle *phdl, uintptr_t addr, u_long val)
{
	int error;

	error = proc_bkptdel(phdl, addr, val);
	ATF_REQUIRE_EQ_MSG(error, 0,
	    "failed to delete breakpoint at 0x%jx", (uintmax_t)addr);

	error = proc_regset(phdl, REG_PC, addr);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to reset program counter");
}

/*
 * Wait for the specified process to hit a breakpoint at the specified symbol.
 */
static void
verify_bkpt(struct proc_handle *phdl, GElf_Sym *sym, const char *symname,
    const char *mapname)
{
	char *name, *mapname_copy, *mapbname;
	GElf_Sym tsym;
	prmap_t *map;
	size_t namesz;
	u_long addr;
	int error, state;

	state = proc_wstatus(phdl);
	ATF_REQUIRE_EQ_MSG(state, PS_STOP, "process has state %d", state);

	/* Get the program counter and decrement it. */
	error = proc_regget(phdl, REG_PC, &addr);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to obtain PC for '%s'",
	    target_prog_file);
	proc_bkptregadj(&addr);

	/*
	 * Make sure the PC matches the expected value obtained from the symbol
	 * definition we looked up earlier.
	 */
	ATF_CHECK_EQ_MSG(addr, sym->st_value,
	    "program counter 0x%lx doesn't match expected value 0x%jx",
	    addr, (uintmax_t)sym->st_value);

	/*
	 * Ensure we can look up the r_debug_state symbol using its starting
	 * address and that the resulting symbol matches the one we found using
	 * a name lookup.
	 */
	namesz = strlen(symname) + 1;
	name = malloc(namesz);
	ATF_REQUIRE(name != NULL);

	error = proc_addr2sym(phdl, addr, name, namesz, &tsym);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to look up symbol at 0x%lx", addr);
	ATF_REQUIRE_EQ(memcmp(sym, &tsym, sizeof(*sym)), 0);
	ATF_REQUIRE_EQ(strcmp(symname, name), 0);
	free(name);

	map = proc_addr2map(phdl, addr);
	ATF_REQUIRE_MSG(map != NULL, "failed to look up map for address 0x%lx",
	    addr);
	mapname_copy = strdup(map->pr_mapname);
	mapbname = basename(mapname_copy);
	ATF_REQUIRE_EQ_MSG(strcmp(mapname, mapbname), 0,
	    "expected map name '%s' doesn't match '%s'", mapname, mapbname);
	free(mapname_copy);
}

ATF_TC(map_alias_name2map);
ATF_TC_HEAD(map_alias_name2map, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Callers are supposed to be able to use \"a.out\" as an alias for "
	    "the program executable. Make sure that proc_name2map() handles "
	    "this properly.");
}
ATF_TC_BODY(map_alias_name2map, tc)
{
	struct proc_handle *phdl;
	prmap_t *map1, *map2;

	phdl = start_prog(tc, false);

	/* Initialize the rtld_db handle. */
	(void)proc_rdagent(phdl);

	/* Ensure that "target_prog" and "a.out" return the same map. */
	map1 = proc_name2map(phdl, target_prog_file);
	ATF_REQUIRE_MSG(map1 != NULL, "failed to look up map for '%s'",
	    target_prog_file);
	map2 = proc_name2map(phdl, aout_object);
	ATF_REQUIRE_MSG(map2 != NULL, "failed to look up map for '%s'",
	    aout_object);
	ATF_CHECK_EQ(strcmp(map1->pr_mapname, map2->pr_mapname), 0);

	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	proc_detach(phdl, 0);
}

ATF_TC(map_prefix_name2map);
ATF_TC_HEAD(map_prefix_name2map, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that proc_name2map() returns prefix matches of the "
	    "basename of loaded objects if no full matches are found.");
}
ATF_TC_BODY(map_prefix_name2map, tc)
{
	struct proc_handle *phdl;
	prmap_t *map1, *map2;

	phdl = start_prog(tc, false);

	/* Initialize the rtld_db handle. */
	(void)proc_rdagent(phdl);

	/* Make sure that "ld-elf" and "ld-elf.so" return the same map. */
	map1 = proc_name2map(phdl, "ld-elf");
	ATF_REQUIRE_MSG(map1 != NULL, "failed to look up map for 'ld-elf'");
	map2 = proc_name2map(phdl, "ld-elf.so");
	ATF_REQUIRE_MSG(map2 != NULL, "failed to look up map for 'ld-elf.so'");
	ATF_CHECK_EQ(strcmp(map1->pr_mapname, map2->pr_mapname), 0);

	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	proc_detach(phdl, 0);
}

ATF_TC(map_alias_name2sym);
ATF_TC_HEAD(map_alias_name2sym, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Callers are supposed to be able to use \"a.out\" as an alias for "
	    "the program executable. Make sure that proc_name2sym() handles "
	    "this properly.");
}
ATF_TC_BODY(map_alias_name2sym, tc)
{
	GElf_Sym sym1, sym2;
	prsyminfo_t si1, si2;
	struct proc_handle *phdl;
	int error;

	phdl = start_prog(tc, false);

	/* Initialize the rtld_db handle. */
	(void)proc_rdagent(phdl);

	/*
	 * Make sure that "target_prog:main" and "a.out:main" return the same
	 * symbol.
	 */
	error = proc_name2sym(phdl, target_prog_file, "main", &sym1, &si1);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to look up 'main' via %s",
	    target_prog_file);
	error = proc_name2sym(phdl, aout_object, "main", &sym2, &si2);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to look up 'main' via %s",
	    aout_object);

	ATF_CHECK_EQ(memcmp(&sym1, &sym2, sizeof(sym1)), 0);
	ATF_CHECK_EQ(si1.prs_id, si2.prs_id);

	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	proc_detach(phdl, 0);
}

ATF_TC(symbol_lookup);
ATF_TC_HEAD(symbol_lookup, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Look up a couple of well-known symbols in the test program, place "
	    "breakpoints on them, and verify that we hit the breakpoints. Also "
	    "make sure that we can use the breakpoint address to look up the "
	    "corresponding symbol.");
}
ATF_TC_BODY(symbol_lookup, tc)
{
	GElf_Sym main_sym, r_debug_state_sym;
	struct proc_handle *phdl;
	u_long saved;
	int error;

	phdl = start_prog(tc, false);

	error = proc_name2sym(phdl, target_prog_file, "main", &main_sym, NULL);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to look up 'main'");

	error = proc_name2sym(phdl, ldelf_object, "r_debug_state",
	    &r_debug_state_sym, NULL);
	ATF_REQUIRE_EQ_MSG(error, 0, "failed to look up 'r_debug_state'");

	set_bkpt(phdl, r_debug_state_sym.st_value, &saved);
	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");
	verify_bkpt(phdl, &r_debug_state_sym, "r_debug_state", ldelf_object);
	remove_bkpt(phdl, r_debug_state_sym.st_value, saved);

	set_bkpt(phdl, main_sym.st_value, &saved);
	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");
	verify_bkpt(phdl, &main_sym, "main", target_prog_file);
	remove_bkpt(phdl, main_sym.st_value, saved);

	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	proc_detach(phdl, 0);
}

ATF_TC(symbol_lookup_fail);
ATF_TC_HEAD(symbol_lookup_fail, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that proc_addr2sym() returns an error when given an offset "
	    "that it cannot resolve.");
}
ATF_TC_BODY(symbol_lookup_fail, tc)
{
	char symname[32];
	GElf_Sym sym;
	struct proc_handle *phdl;
	prmap_t *map;
	int error;

	phdl = start_prog(tc, false);

	/* Initialize the rtld_db handle. */
	(void)proc_rdagent(phdl);

	map = proc_name2map(phdl, target_prog_file);
	ATF_REQUIRE_MSG(map != NULL, "failed to look up map for '%s'",
	    target_prog_file);

	/*
	 * We shouldn't be able to find symbols at the beginning of a mapped
	 * file.
	 */
	error = proc_addr2sym(phdl, map->pr_vaddr, symname, sizeof(symname),
	    &sym);
	ATF_REQUIRE_MSG(error != 0, "unexpectedly found a symbol");

	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	proc_detach(phdl, 0);
}

ATF_TC(signal_forward);
ATF_TC_HEAD(signal_forward, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Run the test program in a mode which causes it to send a signal "
	    "to itself. Make sure that we intercept the signal and that "
	    "proc_continue() forwards it to the process.");
}
ATF_TC_BODY(signal_forward, tc)
{
	struct proc_handle *phdl;
	int state, status;

	phdl = start_prog(tc, true);
	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	/* The process should have been interrupted by a signal. */
	state = proc_wstatus(phdl);
	ATF_REQUIRE_EQ_MSG(state, PS_STOP, "process has unexpected state %d",
	    state);

	/* Continue execution and allow the signal to be delivered. */
	ATF_CHECK_EQ_MSG(proc_continue(phdl), 0, "failed to resume execution");

	/*
	 * Make sure the process exited with status 0. If it didn't receive the
	 * SIGUSR1 that it sent to itself, it'll exit with a non-zero exit
	 * status, causing the test to fail.
	 */
	state = proc_wstatus(phdl);
	ATF_REQUIRE_EQ_MSG(state, PS_UNDEAD, "process has unexpected state %d",
	    state);

	status = proc_getwstat(phdl);
	ATF_REQUIRE(status >= 0);
	ATF_REQUIRE(WIFEXITED(status));
	ATF_REQUIRE_EQ(WEXITSTATUS(status), 0);

	proc_detach(phdl, 0);
}

ATF_TC(symbol_sort_local);
ATF_TC_HEAD(symbol_sort_local, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Ensure that proc_addr2sym() returns the non-local alias when "
	    "the address resolves to multiple symbols.");
}
ATF_TC_BODY(symbol_sort_local, tc)
{
	char symname[32];
	GElf_Sym bar_sym;
	struct proc_handle *phdl;
	int error;

	phdl = start_prog(tc, true);

	error = proc_name2sym(phdl, target_prog_file, "bar", &bar_sym, NULL);
	ATF_REQUIRE_MSG(error == 0, "failed to look up 'bar' in %s",
	    target_prog_file);
	ATF_REQUIRE(GELF_ST_BIND(bar_sym.st_info) == STB_LOCAL);

	error = proc_addr2sym(phdl, bar_sym.st_value, symname, sizeof(symname),
	    &bar_sym);
	ATF_REQUIRE_MSG(error == 0, "failed to resolve 'bar' by addr");

	ATF_REQUIRE_MSG(strcmp(symname, "baz") == 0,
	    "unexpected symbol name '%s'", symname);
	ATF_REQUIRE(GELF_ST_BIND(bar_sym.st_info) == STB_GLOBAL);

	proc_detach(phdl, 0);
}

ATF_TC(symbol_sort_prefix);
ATF_TC_HEAD(symbol_sort_prefix, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Ensure that proc_addr2sym() returns the alias whose name is not "
	    "prefixed with '$' if one exists.");
}
ATF_TC_BODY(symbol_sort_prefix, tc)
{
	char symname[32];
	GElf_Sym qux_sym;
	struct proc_handle *phdl;
	int error;

	phdl = start_prog(tc, true);

	error = proc_name2sym(phdl, target_prog_file, "$qux", &qux_sym, NULL);
	ATF_REQUIRE_MSG(error == 0, "failed to look up '$qux' in %s",
	    target_prog_file);

	error = proc_addr2sym(phdl, qux_sym.st_value, symname, sizeof(symname),
	    &qux_sym);
	ATF_REQUIRE_MSG(error == 0, "failed to resolve 'qux' by addr");

	ATF_REQUIRE_MSG(strcmp(symname, "qux") == 0,
	    "unexpected symbol name '%s'", symname);

	proc_detach(phdl, 0);
}

ATF_TC(symbol_sort_underscore);
ATF_TC_HEAD(symbol_sort_underscore, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Ensure that proc_addr2sym() returns the alias with fewest leading "
	    "underscores in the name when the address resolves to multiple "
	    "symbols.");
}
ATF_TC_BODY(symbol_sort_underscore, tc)
{
	char symname[32];
	GElf_Sym foo_sym;
	struct proc_handle *phdl;
	int error;

	phdl = start_prog(tc, true);

	error = proc_name2sym(phdl, target_prog_file, "foo", &foo_sym, NULL);
	ATF_REQUIRE_MSG(error == 0, "failed to look up 'foo' in %s",
	    target_prog_file);

	error = proc_addr2sym(phdl, foo_sym.st_value, symname, sizeof(symname),
	    &foo_sym);
	ATF_REQUIRE_MSG(error == 0, "failed to resolve 'foo' by addr");

	ATF_REQUIRE_MSG(strcmp(symname, "foo") == 0,
	    "unexpected symbol name '%s'", symname);

	proc_detach(phdl, 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, map_alias_name2map);
	ATF_TP_ADD_TC(tp, map_prefix_name2map);
	ATF_TP_ADD_TC(tp, map_alias_name2sym);
	ATF_TP_ADD_TC(tp, symbol_lookup);
	ATF_TP_ADD_TC(tp, symbol_lookup_fail);
	ATF_TP_ADD_TC(tp, signal_forward);
	ATF_TP_ADD_TC(tp, symbol_sort_local);
	ATF_TP_ADD_TC(tp, symbol_sort_prefix);
	ATF_TP_ADD_TC(tp, symbol_sort_underscore);

	return (atf_no_error());
}
