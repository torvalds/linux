/*	$OpenBSD: cmd.c,v 1.186 2025/07/31 13:37:06 krw Exp $	*/

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
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid.h>

#include "part.h"
#include "disk.h"
#include "misc.h"
#include "mbr.h"
#include "gpt.h"
#include "user.h"
#include "cmd.h"

int		 gedit(const int);
int		 edit(const int, struct mbr *);
int		 gsetpid(const int);
int		 setpid(const int, struct mbr *);
int		 parsepn(const char *);
int		 parseflag(const char *, uint64_t *);

int		 ask_num(const char *, int, int, int);
int		 ask_pid(const int);
const struct uuid *ask_uuid(const struct uuid *);

extern const unsigned char	manpage[];
extern const int		manpage_sz;

int
Xreinit(const char *args, struct mbr *mbr)
{
	int			dogpt;

	dogpt = 0;

	if (strcasecmp(args, "gpt") == 0)
		dogpt = 1;
	else if (strcasecmp(args, "mbr") == 0)
		dogpt = 0;
	else if (strlen(args) > 0) {
		printf("Unrecognized modifier '%s'\n", args);
		return CMD_CONT;
	}

	if (dogpt) {
		GPT_init(GHANDGP);
		GPT_print("s");
	} else {
		MBR_init(mbr);
		MBR_print(mbr, "s");
	}

	printf("Use 'write' to update disk.\n");

	return CMD_DIRTY;
}

int
Xswap(const char *args, struct mbr *mbr)
{
	char			 lbuf[LINEBUFSZ];
	char			*from, *to;
	int			 pf, pt;
	struct prt		 pp;
	struct gpt_partition	 gg;

	if (strlcpy(lbuf, args, sizeof(lbuf)) >= sizeof(lbuf)) {
		printf("argument string too long\n");
		return CMD_CONT;
	}

	to = lbuf;
	from = strsep(&to, WHITESPACE);

	pt = parsepn(to);
	if (pt == -1)
		return CMD_CONT;

	pf = parsepn(from);
	if (pf == -1)
		return CMD_CONT;

	if (pt == pf) {
		printf("%d same partition as %d, doing nothing.\n", pt, pf);
		return CMD_CONT;
	}

	if (gh.gh_sig == GPTSIGNATURE) {
		gg = gp[pt];
		gp[pt] = gp[pf];
		gp[pf] = gg;
	} else {
		pp = mbr->mbr_prt[pt];
		mbr->mbr_prt[pt] = mbr->mbr_prt[pf];
		mbr->mbr_prt[pf] = pp;
	}

	return CMD_DIRTY;
}

int
gedit(const int pn)
{
	struct uuid		 oldtype;

	oldtype = gp[pn].gp_type;

	if (gsetpid(pn))
		return -1;

	if (uuid_is_nil(&gp[pn].gp_type, NULL)) {
		if (uuid_is_nil(&oldtype, NULL) == 0) {
			memset(&gp[pn], 0, sizeof(gp[pn]));
			printf("Partition %d is disabled.\n", pn);
		}
		return 0;
	}

	if (GPT_get_lba_start(pn) == -1 ||
	    GPT_get_lba_end(pn) == -1 ||
	    GPT_get_name(pn) == -1) {
		return -1;
	}

	return 0;
}

int
parsepn(const char *pnstr)
{
	const char		*errstr;
	int			 maxpn, pn;

	if (pnstr == NULL) {
		printf("no partition number\n");
		return -1;
	}

	if (gh.gh_sig == GPTSIGNATURE)
		maxpn = gh.gh_part_num - 1;
	else
		maxpn = NDOSPART - 1;

	pn = strtonum(pnstr, 0, maxpn, &errstr);
	if (errstr) {
		printf("partition number is %s: %s\n", errstr, pnstr);
		return -1;
	}

	return pn;
}

int
parseflag(const char *flagstr, uint64_t *flagvalue)
{
	const char		*errstr;
	char			*ep;
	uint64_t		 val;

	flagstr += strspn(flagstr, WHITESPACE);
	if (flagstr[0] == '0' && (flagstr[1] == 'x' || flagstr[1] == 'X')) {
		errno = 0;
		val = strtoull(flagstr, &ep, 16);
		if (errno || ep == flagstr || *ep != '\0' ||
		    (gh.gh_sig != GPTSIGNATURE && val > 0xff)) {
			printf("flag value is invalid: %s\n", flagstr);
			return -1;
		}
		goto done;
	}

	if (gh.gh_sig == GPTSIGNATURE)
		val = strtonum(flagstr, 0, INT64_MAX, &errstr);
	else
		val = strtonum(flagstr, 0, 0xff, &errstr);
	if (errstr) {
		printf("flag value is %s: %s\n", errstr, flagstr);
		return -1;
	}

 done:
	*flagvalue = val;
	return 0;
}

int
edit(const int pn, struct mbr *mbr)
{
	struct chs		 start, end;
	struct prt		*pp;
	uint64_t		 track;
	unsigned char		 oldid;

	pp = &mbr->mbr_prt[pn];
	oldid = pp->prt_id;

	if (setpid(pn, mbr))
		return -1;

	if (pp->prt_id == DOSPTYP_UNUSED) {
		if (oldid != DOSPTYP_UNUSED) {
			memset(pp, 0, sizeof(*pp));
			printf("Partition %d is disabled.\n", pn);
		}
		return 0;
	}

	if (ask_yn("Do you wish to edit in CHS mode?")) {
		PRT_lba_to_chs(pp, &start, &end);
		start.chs_cyl = ask_num("BIOS Starting cylinder", start.chs_cyl,
		    0, disk.dk_cylinders - 1);
		start.chs_head = ask_num("BIOS Starting head", start.chs_head,
		    0, disk.dk_heads - 1);
		start.chs_sect = ask_num("BIOS Starting sector", start.chs_sect,
		    1, disk.dk_sectors);

		end.chs_cyl = ask_num("BIOS Ending cylinder", end.chs_cyl,
		    start.chs_cyl, disk.dk_cylinders - 1);
		end.chs_head = ask_num("BIOS Ending head", end.chs_head,
		    (start.chs_cyl == end.chs_cyl) ? start.chs_head : 0,
		    disk.dk_heads - 1);
		end.chs_sect = ask_num("BIOS Ending sector", end.chs_sect,
		    (start.chs_cyl == end.chs_cyl && start.chs_head ==
		    end.chs_head) ? start.chs_sect : 1, disk.dk_sectors);

		/* The ATA/ATAPI spec says LBA = (C × HPC + H) × SPT + (S − 1) */
		track = start.chs_cyl * disk.dk_heads + start.chs_head;
		pp->prt_bs = track * disk.dk_sectors + (start.chs_sect - 1);
		track = end.chs_cyl * disk.dk_heads + end.chs_head;
		pp->prt_ns = track * disk.dk_sectors + (end.chs_sect - 1) -
		    pp->prt_bs + 1;
	} else {
		pp->prt_bs = getuint64("Partition offset", pp->prt_bs, 0,
		    disk.dk_size - 1);
		pp->prt_ns = getuint64("Partition size",   pp->prt_ns, 1,
		    disk.dk_size - pp->prt_bs);
	}

	return 0;
}

int
Xedit(const char *args, struct mbr *mbr)
{
	struct gpt_partition	 oldgg;
	struct prt		 oldprt;
	int			 pn;

	if (gh.gh_sig == GPTSIGNATURE && strchr(args, ':')) {
		if (GPT_recover_partition(args, "", "") == 0)
			return CMD_DIRTY;	/* New UUID was generated. */
		else {
			printf("invalid description or insufficient space\n");
			return CMD_CONT;
		}
	}

	pn = parsepn(args);
	if (pn == -1)
		return CMD_CONT;

	if (gh.gh_sig == GPTSIGNATURE) {
		oldgg = gp[pn];
		if (gedit(pn))
			gp[pn] = oldgg;
		else if (memcmp(&gp[pn], &oldgg, sizeof(gp[pn])))
			return CMD_DIRTY;
	} else {
		oldprt = mbr->mbr_prt[pn];
		if (edit(pn, mbr))
			mbr->mbr_prt[pn] = oldprt;
		else if (memcmp(&mbr->mbr_prt[pn], &oldprt, sizeof(oldprt)))
			return CMD_DIRTY;
	}

	return CMD_CONT;
}

int
gsetpid(const int pn)
{
	int32_t			 is_nil;
	uint32_t		 status;

	GPT_print_parthdr();
	GPT_print_part(pn, "s");

	if (PRT_protected_uuid(&gp[pn].gp_type)) {
		printf("can't edit partition type %s\n",
		    PRT_uuid_to_desc(&gp[pn].gp_type, 0));
		return -1;
	}

	is_nil = uuid_is_nil(&gp[pn].gp_type, NULL);
	gp[pn].gp_type = *ask_uuid(&gp[pn].gp_type);
	if (PRT_protected_uuid(&gp[pn].gp_type) && is_nil == 0) {
		printf("can't change partition type to %s\n",
		    PRT_uuid_to_desc(&gp[pn].gp_type, 0));
		return -1;
	}

	if (uuid_is_nil(&gp[pn].gp_guid, NULL)) {
		uuid_create(&gp[pn].gp_guid, &status);
		if (status != uuid_s_ok) {
			printf("could not create guid for partition\n");
			return -1;
		}
	}

	return 0;
}

int
setpid(const int pn, struct mbr *mbr)
{
	struct prt		*pp;

	pp = &mbr->mbr_prt[pn];

	PRT_print_parthdr();
	PRT_print_part(pn, pp, "s");

	pp->prt_id = ask_pid(pp->prt_id);

	return 0;
}

int
Xsetpid(const char *args, struct mbr *mbr)
{
	struct gpt_partition	oldgg;
	struct prt		oldprt;
	int			pn;

	pn = parsepn(args);
	if (pn == -1)
		return CMD_CONT;

	if (gh.gh_sig == GPTSIGNATURE) {
		oldgg = gp[pn];
		if (gsetpid(pn))
			gp[pn] = oldgg;
		else if (memcmp(&gp[pn], &oldgg, sizeof(gp[pn])))
			return CMD_DIRTY;
	} else {
		oldprt = mbr->mbr_prt[pn];
		if (setpid(pn, mbr))
			mbr->mbr_prt[pn] = oldprt;
		else if (memcmp(&mbr->mbr_prt[pn], &oldprt, sizeof(oldprt)))
			return CMD_DIRTY;
	}

	return CMD_CONT;
}

int
Xselect(const char *args, struct mbr *mbr)
{
	static uint64_t		lba_firstembr = 0;
	uint64_t		lba_self;
	int			pn;

	pn = parsepn(args);
	if (pn == -1)
		return CMD_CONT;

	lba_self = mbr->mbr_prt[pn].prt_bs;

	if ((mbr->mbr_prt[pn].prt_id != DOSPTYP_EXTEND) &&
	    (mbr->mbr_prt[pn].prt_id != DOSPTYP_EXTENDL)) {
		printf("Partition %d is not an extended partition.\n", pn);
		return CMD_CONT;
	}

	if (lba_firstembr == 0)
		lba_firstembr = lba_self;

	if (lba_self == 0) {
		printf("Loop to MBR (sector 0)! Not selected.\n");
		return CMD_CONT;
	} else {
		printf("Selected extended partition %d\n", pn);
		printf("New EMBR at offset %llu.\n", lba_self);
	}

	USER_edit(lba_self, lba_firstembr);

	return CMD_CONT;
}

int
Xprint(const char *args, struct mbr *mbr)
{
	if (gh.gh_sig == GPTSIGNATURE)
		GPT_print(args);
	else if (MBR_valid_prt(mbr))
		MBR_print(mbr, args);
	else {
		DISK_printgeometry("s");
		printf("Offset: %d\tSignature: 0x%X.\tNo MBR or GPT.\n",
		    DOSBBSECTOR, (int)mbr->mbr_signature);
	}

	return CMD_CONT;
}

int
Xwrite(const char *args, struct mbr *mbr)
{
	unsigned int		i, n;

	for (i = 0, n = 0; i < nitems(mbr->mbr_prt); i++)
		if (mbr->mbr_prt[i].prt_id == DOSPTYP_OPENBSD)
			n++;
	if (n > 1) {
		warnx("MBR contains more than one OpenBSD partition!");
		if (ask_yn("Write MBR anyway?") == 0)
			return CMD_CONT;
	}

	if (gh.gh_sig == GPTSIGNATURE) {
		printf("Writing GPT.\n");
		if (GPT_write() == -1) {
			warnx("error writing GPT");
			return CMD_CONT;
		}
	} else {
		printf("Writing MBR at offset %llu.\n", mbr->mbr_lba_self);
		if (MBR_write(mbr) == -1) {
			warnx("error writing MBR");
			return CMD_CONT;
		}
		GPT_zap_headers();
	}

	return CMD_CLEAN;
}

int
Xquit(const char *args, struct mbr *mbr)
{
	return CMD_QUIT;
}

int
Xabort(const char *args, struct mbr *mbr)
{
	exit(0);
}

int
Xexit(const char *args, struct mbr *mbr)
{
	return CMD_EXIT;
}

int
Xhelp(const char *args, struct mbr *mbr)
{
	USER_help(mbr);

	return CMD_CONT;
}

int
Xupdate(const char *args, struct mbr *mbr)
{
	memcpy(mbr->mbr_code, default_dmbr.dmbr_boot, sizeof(mbr->mbr_code));
	mbr->mbr_signature = DOSMBR_SIGNATURE;
	printf("Machine code updated.\n");
	return CMD_DIRTY;
}

int
Xflag(const char *args, struct mbr *mbr)
{
	char			 lbuf[LINEBUFSZ];
	char			*part, *flag;
	uint64_t		 val;
	int			 i, pn;

	if (strlcpy(lbuf, args, sizeof(lbuf)) >= sizeof(lbuf)) {
		printf("argument string too long\n");
		return CMD_CONT;
	}

	flag = lbuf;
	part = strsep(&flag, WHITESPACE);

	pn = parsepn(part);
	if (pn == -1)
		return CMD_CONT;

	if (flag != NULL) {
		if (parseflag(flag, &val) == -1)
			return CMD_CONT;
		if (gh.gh_sig == GPTSIGNATURE)
			gp[pn].gp_attrs = val;
		else
			mbr->mbr_prt[pn].prt_flag = val;
		printf("Partition %d flag value set to 0x%llx.\n", pn, val);
	} else {
		if (gh.gh_sig == GPTSIGNATURE) {
			for (i = 0; i < gh.gh_part_num; i++) {
				if (i == pn)
					gp[i].gp_attrs = GPTPARTATTR_BOOTABLE;
				else
					gp[i].gp_attrs &= ~GPTPARTATTR_BOOTABLE;
			}
		} else {
			for (i = 0; i < nitems(mbr->mbr_prt); i++) {
				if (i == pn)
					mbr->mbr_prt[i].prt_flag = DOSACTIVE;
				else
					mbr->mbr_prt[i].prt_flag = 0x00;
			}
		}
		printf("Partition %d marked active.\n", pn);
	}

	return CMD_DIRTY;
}

int
Xmanual(const char *args, struct mbr *mbr)
{
	char			*pager = "/usr/bin/less";
	char			*p;
	FILE			*f;
	sig_t			 opipe;

	opipe = signal(SIGPIPE, SIG_IGN);
	if ((p = getenv("PAGER")) != NULL && (*p != '\0'))
		pager = p;
	if (asprintf(&p, "gunzip -qc|%s", pager) != -1) {
		f = popen(p, "w");
		if (f) {
			fwrite(manpage, manpage_sz, 1, f);
			pclose(f);
		}
		free(p);
	}

	signal(SIGPIPE, opipe);

	return CMD_CONT;
}

int
ask_num(const char *str, int dflt, int low, int high)
{
	char			 lbuf[LINEBUFSZ];
	const char		*errstr;
	int			 num;

	if (dflt < low)
		dflt = low;
	else if (dflt > high)
		dflt = high;

	do {
		printf("%s [%d - %d]: [%d] ", str, low, high, dflt);
		string_from_line(lbuf, sizeof(lbuf), TRIMMED);

		if (lbuf[0] == '\0') {
			num = dflt;
			errstr = NULL;
		} else {
			num = (int)strtonum(lbuf, low, high, &errstr);
			if (errstr)
				printf("%s is %s: %s.\n", str, errstr, lbuf);
		}
	} while (errstr);

	return num;
}

int
ask_pid(const int dflt)
{
	char			lbuf[LINEBUFSZ];
	int			num;

	for (;;) {
		printf("Partition id ('0' to disable) [01 - FF]: [%02X] ", dflt);
		printf("(? for help) ");
		string_from_line(lbuf, sizeof(lbuf), TRIMMED);

		if (strlen(lbuf) == 0)
			return dflt;
		if (strcmp(lbuf, "?") == 0) {
			PRT_print_mbrmenu();
			continue;
		}

		num = hex_octet(lbuf);
		if (num != -1)
			return num;

		printf("'%s' is not a valid partition id.\n", lbuf);
	}
}

const struct uuid *
ask_uuid(const struct uuid *olduuid)
{
	char			 lbuf[LINEBUFSZ];
	static struct uuid	 uuid;
	const char		*guid;
	const char		*dflt;

	dflt = PRT_uuid_to_desc(olduuid, 1);	/* guid, menu id or "00". */
	for (;;) {
		printf("Partition id ('0' to disable) [01 - FF, <uuid>]: [%s] ",
		    dflt);
		printf("(? for help) ");
		string_from_line(lbuf, sizeof(lbuf), TRIMMED);

		if (strcmp(lbuf, "?") == 0) {
			PRT_print_gptmenu();
			continue;
		} else if (strlen(lbuf) == 0) {
			uuid = *olduuid;
			goto done;
		}

		guid = PRT_desc_to_guid(lbuf);
		if (guid == NULL)
			guid = lbuf;

		if (string_to_uuid(guid, &uuid) == uuid_s_ok)
			goto done;

		printf("'%s' has no associated UUID\n", lbuf);
	}

 done:
	return &uuid;
}
