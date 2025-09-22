/*	$OpenBSD: user.c,v 1.86 2025/06/26 13:33:44 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/disklabel.h>

#include <err.h>
#include <stdio.h>
#include <string.h>

#include "part.h"
#include "mbr.h"
#include "misc.h"
#include "cmd.h"
#include "user.h"
#include "gpt.h"
#include "disk.h"

struct cmd {
	char	*cmd_name;
	int	 cmd_valid;
	int	(*cmd_fcn)(const char *, struct mbr *);
	char	*cmd_help;
};

const struct cmd		cmd_table[] = {
	{"help",   2, Xhelp,   "Display summary of available commands"},
	{"manual", 2, Xmanual, "Display fdisk man page"},
	{"reinit", 2, Xreinit, "Initialize the partition table"},
	{"setpid", 1, Xsetpid, "Set identifier of table entry"},
	{"edit",   1, Xedit,   "Edit table entry"},
	{"flag",   1, Xflag,   "Set flag value of table entry"},
	{"update", 0, Xupdate, "Update MBR bootcode"},
	{"select", 0, Xselect, "Select MBR extended table entry"},
	{"swap",   1, Xswap,   "Swap two table entries"},
	{"print",  2, Xprint,  "Print partition table"},
	{"write",  1, Xwrite,  "Write partition table to disk"},
	{"exit",   1, Xexit,   "Discard changes and exit edit level"},
	{"quit",   1, Xquit,   "Save changes and exit edit level"},
	{"abort",  2, Xabort,  "Discard changes and terminate fdisk"},
};

#define	ANY_CMD(_i)	(cmd_table[(_i)].cmd_valid > 1)
#define	GPT_CMD(_i)	(cmd_table[(_i)].cmd_valid > 0)

int			modified;

int			ask_cmd(const struct mbr *, char **);

void
USER_edit(const uint64_t lba_self, const uint64_t lba_firstembr)
{
	struct mbr		 mbr;
	char			*args;
	int			 i, st;
	static int		 editlevel;

	if (MBR_read(lba_self, lba_firstembr, &mbr))
		return;

	editlevel += 1;

	if (editlevel == 1)
		GPT_read(ANYGPT);

	printf("Enter 'help' for information\n");

	for (;;) {
		if (gh.gh_sig == GPTSIGNATURE && editlevel > 1)
			break;	/* 'reinit gpt'. Unwind recursion! */

		printf("%s%s: %d> ", disk.dk_name, modified ? "*" : "",
		    editlevel);
		fflush(stdout);
		i = ask_cmd(&mbr, &args);
		if (i == -1)
			continue;

		st = cmd_table[i].cmd_fcn(args ? args : "", &mbr);

		if (st == CMD_EXIT) {
			if (modified)
				printf("Aborting changes to current MBR\n");
			break;
		}
		if (st == CMD_QUIT) {
			if (modified && Xwrite(NULL, &mbr) == CMD_CONT)
				continue;
			break;
		}
		if (st == CMD_CLEAN)
			modified = 0;
		if (st == CMD_DIRTY)
			modified = 1;
	}

	editlevel -= 1;
}

void
USER_print_disk(void)
{
	struct mbr		mbr;
	uint64_t		lba_self, lba_firstembr;
	unsigned int		i;

	lba_self = lba_firstembr = 0;

	do {
		if (MBR_read(lba_self, lba_firstembr, &mbr))
			break;
		if (lba_self == DOSBBSECTOR) {
			if (MBR_valid_prt(&mbr) == 0) {
				DISK_printgeometry("s");
				printf("Offset: %d\tSignature: 0x%X.\t"
				    "No MBR or GPT.\n", DOSBBSECTOR,
				    (int)mbr.mbr_signature);
				return;
			}
			if (GPT_read(ANYGPT)) {
				if (verbosity == VERBOSE) {
					printf("Primary GPT:\nNot Found\n");
					printf("\nSecondary GPT:\nNot Found\n");
				}
			} else if (verbosity == TERSE) {
				GPT_print("s");
				return;
			} else {
				printf("Primary GPT:\n");
				GPT_read(PRIMARYGPT);
				if (gh.gh_sig == GPTSIGNATURE)
					GPT_print("s");
				else
					printf("\tNot Found\n");
				printf("\nSecondary GPT:\n");
				GPT_read(SECONDARYGPT);
				if (gh.gh_sig == GPTSIGNATURE)
					GPT_print("s");
				else
					printf("\tNot Found\n");
			}
			if (verbosity == VERBOSE)
				printf("\nMBR:\n");
		}

		MBR_print(&mbr, "s");

		for (lba_self = i = 0; i < nitems(mbr.mbr_prt); i++)
			if (mbr.mbr_prt[i].prt_id == DOSPTYP_EXTEND ||
			    mbr.mbr_prt[i].prt_id == DOSPTYP_EXTENDL) {
				lba_self = mbr.mbr_prt[i].prt_bs;
				if (lba_firstembr == 0)
					lba_firstembr = lba_self;
			}
	} while (lba_self);
}

void
USER_help(const struct mbr *mbr)
{
	unsigned int		i;

	for (i = 0; i < nitems(cmd_table); i++) {
		if (gh.gh_sig == GPTSIGNATURE && GPT_CMD(i) == 0)
				continue;
		if (MBR_valid_prt(mbr) == 0 && ANY_CMD(i) == 0)
			continue;
		printf("\t%s\t\t%s\n", cmd_table[i].cmd_name,
		    cmd_table[i].cmd_help);
	}
}

int
ask_cmd(const struct mbr *mbr, char **arg)
{
	static char		 lbuf[LINEBUFSZ];
	char			*cmd;
	unsigned int		 i;

	string_from_line(lbuf, sizeof(lbuf), TRIMMED);

	*arg = lbuf;
	cmd = strsep(arg, WHITESPACE);

	if (*arg != NULL)
		*arg += strspn(*arg, WHITESPACE);

	if (strlen(cmd) == 0)
		return -1;
	if (strcmp(cmd, "?") == 0)
		cmd = "help";

	for (i = 0; i < nitems(cmd_table); i++) {
		if (gh.gh_sig == GPTSIGNATURE && GPT_CMD(i) == 0)
			continue;
		if (MBR_valid_prt(mbr) == 0 && ANY_CMD(i) == 0)
			continue;
		if (strstr(cmd_table[i].cmd_name, cmd) == cmd_table[i].cmd_name)
			return i;
	}

	printf("Invalid command '%s", cmd);
	if (*arg && strlen(*arg) > 0)
		printf(" %s", *arg);
	printf("'. Try 'help'.\n");

	return -1;
}
