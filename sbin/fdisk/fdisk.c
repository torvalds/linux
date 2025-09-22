/*	$OpenBSD: fdisk.c,v 1.149 2025/06/29 16:15:52 krw Exp $	*/

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

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "part.h"
#include "disk.h"
#include "mbr.h"
#include "cmd.h"
#include "misc.h"
#include "user.h"
#include "gpt.h"

#define	INIT_GPT		1
#define	INIT_GPTPARTITIONS	2
#define	INIT_MBR		3
#define	INIT_MBRBOOTCODE	4
#define	INIT_RECOVER		5

#define	_PATH_MBR		_PATH_BOOTDIR "mbr"

int			y_flag;
int			verbosity = TERSE;

void			parse_bootprt(const char *);
void			get_default_dmbr(const char *);
void			recover(const char *, struct mbr *);
void			recover_disk_gpt(void);
void			recover_file_gpt(FILE *);
void			recover_file_mbr(FILE *, struct mbr *);

static void
usage(void)
{
	extern char		* __progname;

	fprintf(stderr, "usage: %s "
	    "[-evy] [-A | -g | -i | -u] [-b blocks[@offset[:type]]]\n"
	    "\t[-l blocks | -c cylinders -h heads -s sectors] [-f file] "
	    "disk\n", __progname);
	fprintf(stderr, "       %s -R [-evy] disk [file]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct mbr		 mbr;
	const char		*mbrfile = NULL, *recoverfile = NULL;
	const char		*errstr;
	int			 ch;
	int			 e_flag = 0, init = 0;
	int			 oflags = O_RDONLY;

	while ((ch = getopt(argc, argv, "Ab:c:ef:gh:il:Rs:uvy")) != -1) {
		switch(ch) {
		case 'A':
			init = INIT_GPTPARTITIONS;
			break;
		case 'b':
			parse_bootprt(optarg);
			if (disk.dk_bootprt.prt_id != DOSPTYP_EFISYS)
				disk.dk_bootprt.prt_flag = DOSACTIVE;
			break;
		case 'c':
			disk.dk_cylinders = strtonum(optarg, 1, 262144, &errstr);
			if (errstr)
				errx(1, "Cylinder argument %s [1..262144].",
				    errstr);
			disk.dk_size = 0;
			break;
		case 'e':
			e_flag = 1;
			break;
		case 'f':
			mbrfile = optarg;
			break;
		case 'g':
			init = INIT_GPT;
			break;
		case 'h':
			disk.dk_heads = strtonum(optarg, 1, 256, &errstr);
			if (errstr)
				errx(1, "Head argument %s [1..256].", errstr);
			disk.dk_size = 0;
			break;
		case 'i':
			init = INIT_MBR;
			break;
		case 'l':
			disk.dk_size = strtonum(optarg, BLOCKALIGNMENT,
			    UINT32_MAX, &errstr);
			if (errstr)
				errx(1, "Block argument %s [%u..%u].", errstr,
				    BLOCKALIGNMENT, UINT32_MAX);
			disk.dk_cylinders = disk.dk_heads = disk.dk_sectors = 0;
			break;
		case 'R':
			init = INIT_RECOVER;
			break;
		case 's':
			disk.dk_sectors = strtonum(optarg, 1, 63, &errstr);
			if (errstr)
				errx(1, "Sector argument %s [1..63].", errstr);
			disk.dk_size = 0;
			break;
		case 'u':
			init = INIT_MBRBOOTCODE;
			break;
		case 'v':
			verbosity = VERBOSE;
			break;
		case 'y':
			y_flag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (init == INIT_RECOVER && argc == 2) {
		recoverfile = argv[1];
		argc = 1;	/* Leaving argv[0] holding the disk name. */
	}

	if (argc != 1)
		usage();

	if ((disk.dk_cylinders || disk.dk_heads || disk.dk_sectors) &&
	    (disk.dk_cylinders * disk.dk_heads * disk.dk_sectors == 0))
		usage();

	if (init || e_flag)
		oflags = O_RDWR;

	get_default_dmbr(mbrfile);

	DISK_open(argv[0], oflags);
	if (oflags == O_RDONLY) {
		if (pledge("stdio", NULL) == -1)
			err(1, "pledge");
		USER_print_disk();
		goto done;
	}

	/*
	 * "stdio" to talk to the outside world.
	 * "proc exec" for man page display.
	 * "disklabel" for DIOCRLDINFO.
	 * "rpath" for INIT_RECOVER
	 */
	if (pledge("stdio disklabel proc exec rpath", NULL) == -1)
		err(1, "pledge");

	switch (init) {
	case INIT_RECOVER:
		recover(recoverfile, &mbr);
		if (gh.gh_sig == GPTSIGNATURE) {
			if (ask_yn("Do you wish to write recovered GPT?"))
				Xwrite(NULL, &gmbr);
		} else if (mbr.mbr_signature == DOSMBR_SIGNATURE) {
			if (ask_yn("Do you wish to write recovered MBR?"))
				Xwrite(NULL, &mbr);
		} else if (recoverfile == NULL) {
			errx(1, "no GPT partitions found on %s", disk.dk_name);
		} else
			errx(1, "no partition info found in %s", recoverfile);
		break;
	case INIT_GPT:
		if (GPT_init(GHANDGP))
			errx(1, "-g could not create valid GPT");
		if (ask_yn("Do you wish to write new GPT?"))
			Xwrite(NULL, &gmbr);
		break;
	case INIT_GPTPARTITIONS:
		if (GPT_read(ANYGPT))
			errx(1, "-A requires a valid GPT");
		if (GPT_init(GPONLY))
			errx(1, "-A could not create valid GPT");
		if (ask_yn("Do you wish to write new GPT?"))
			Xwrite(NULL, &gmbr);
		break;
	case INIT_MBR:
		mbr.mbr_lba_self = mbr.mbr_lba_firstembr = 0;
		MBR_init(&mbr);
		if (ask_yn("Do you wish to write new MBR?"))
			Xwrite(NULL, &mbr);
		break;
	case INIT_MBRBOOTCODE:
		if (GPT_read(ANYGPT) == 0)
			errx(1, "-u not available for GPT");
		if (MBR_read(0, 0, &mbr))
			errx(1, "Can't read MBR!");
		memcpy(mbr.mbr_code, default_dmbr.dmbr_boot,
		    sizeof(mbr.mbr_code));
		if (ask_yn("Do you wish to write new MBR?"))
			Xwrite(NULL, &mbr);
		break;
	default:
		break;
	}

	if (e_flag)
		USER_edit(0, 0);

done:
	close(disk.dk_fd);

	return 0;
}

void
parse_bootprt(const char *arg)
{
	const char		*errstr;
	char			*poffset, *ptype;
	int			 partitiontype;
	uint32_t		 blockcount, blockoffset;

	blockoffset = BLOCKALIGNMENT;
	partitiontype = DOSPTYP_EFISYS;
	ptype = NULL;

	/* First number: # of 512-byte blocks in boot partition. */
	poffset = strchr(arg, '@');
	if (poffset != NULL)
		*poffset++ = '\0';
	if (poffset != NULL) {
		ptype = strchr(poffset, ':');
		if (ptype != NULL)
			*ptype++ = '\0';
	}

	blockcount = strtonum(arg, 1, UINT32_MAX, &errstr);
	if (errstr)
		errx(1, "Block argument %s [%u..%u].", errstr, 1, UINT32_MAX);

	if (poffset == NULL)
		goto done;

	/* Second number: # of 512-byte blocks to offset partition start. */
	blockoffset = strtonum(poffset, 1, UINT32_MAX, &errstr);
	if (errstr)
		errx(1, "Block offset argument %s [%u..%u].", errstr, 1,
		    UINT32_MAX);

	if (ptype == NULL)
		goto done;

	partitiontype = hex_octet(ptype);
	if (partitiontype == -1)
		errx(1, "Block type is not a 1-2 digit hex value");

 done:
	disk.dk_bootprt.prt_ns = blockcount;
	disk.dk_bootprt.prt_bs = blockoffset;
	disk.dk_bootprt.prt_id = partitiontype;
}

void
get_default_dmbr(const char *mbrfile)
{
	struct dos_mbr		*dmbr = &default_dmbr;
	ssize_t			 len, sz;
	int			 fd;

	if (mbrfile == NULL)
#ifdef HAS_MBR
		mbrfile = _PATH_MBR;
#else
		return;
#endif

	fd = open(mbrfile, O_RDONLY);
	if (fd == -1)
		err(1, "%s", mbrfile);

	sz = sizeof(*dmbr);
	len = read(fd, dmbr, sz);
	if (len == -1)
		err(1, "read('%s')", mbrfile);
	else if (len != sz)
		errx(1, "read('%s'): read %zd bytes of %zd", mbrfile, len, sz);

	close(fd);
}

void
recover_disk_gpt(void)
{
	unsigned int recovered, pn;

	GPT_init(GHANDGP);
	memset(&gp, 0, sizeof(gp));	/* Discard default OpenBSD partition. */

	recovered = 0;
	if (GPT_recover_partition(NULL, NULL, NULL) == -1)
		goto done;

	for (pn = 0, recovered = 0; pn < nitems(gp); pn++) {
		if (gp[pn].gp_lba_start >= letoh64(gh.gh_lba_start))
			recovered++;
	}

 done:
	if (recovered == 0)
		memset(&gh, 0, sizeof(gh));
	else
		GPT_print("s");
}

void
recover_file_mbr(FILE *fp, struct mbr *mbr)
{
	char			*line = NULL;
	size_t			 linesize = 0;
	unsigned int		 recovered = 0;

	MBR_init(mbr);
	memset(mbr->mbr_prt, 0, sizeof(mbr->mbr_prt));

	while (getline(&line, &linesize, fp) != -1) {
		if (linesize < 3)
			continue;
		if (line[0] != '*' && line[0] != ' ')
			continue;
		if (!isdigit((unsigned int)line[1]) || line[2] != ':')
			continue;
		if (MBR_recover_partition(line, mbr) == -1)
			goto done;
		recovered++;
	}

 done:
	free(line);
	if (recovered == 0)
		memset(mbr, 0, sizeof(*mbr));
	else
		MBR_print(mbr, "s");
}

void
recover_file_gpt(FILE *fp)
{
	char			 l1[128], l2[LINEBUFSZ], l3[LINEBUFSZ];
	char			*start, *line = NULL;
	size_t			 whitespace, linesize = 0;
	unsigned int		 recovered = 0;
	int			 digits;

	GPT_init(GHANDGP);
	memset(&gp, 0, sizeof(gp));	/* Discard default OpenBSD partition. */
	l1[0] = l2[0] = l3[0] = '\0';

	/*
	 * Lines of interest begin with one of
	 *
	 * 1) '<1-3 digits>:', possible partition start (put in l1)
	 * 2) '<8 hex digits>-', possible UUID line (put in l2)
	 * 3) 'Attributes:', the attributes line of fdisk -v (put in l3)
	 *
	 * Other lines are ignored.
	 *
	 * Recovery stops at first parsing failure.
	 */
	for (;;) {
		if (getline(&line, &linesize, fp) == -1) {
			if (GPT_recover_partition(l1, l2, l3) != -1)
				recovered++;
			goto done;
		}
		whitespace = strspn(line, WHITESPACE);
		if (whitespace >= linesize)
			continue;
		start = line + whitespace;

		digits = strspn(start, "0123456789");
		if (digits > 0 && digits < 4 && *(start+digits) == ':') {
			if (strlen(l1)) {
				if (GPT_recover_partition(l1, l2, l3) == -1)
					goto done;
				recovered++;
			}
			if (strlcpy(l1, start, sizeof(l1)) >= sizeof(l1))
				goto done;
			l2[0] = l3[0] = '\0';
			continue;
		}

		digits = strspn(start, "0123456789abcdefABCDEF");
		if (digits == 8 && *(start+digits) == '-') {
			if (strlcpy(l2, start, sizeof(l2)) >= sizeof(l2))
			    goto done;
			continue;
		}

		if (strncmp(start, "Attributes:", 11) == 0) {
			if (strlcpy(l3, start, sizeof(l3)) >= sizeof(l3))
				goto done;
			continue;
		}
	}

 done:
	free(line);
	if (recovered == 0)
		memset(&gh, 0, sizeof(gh));
	else
		GPT_print("s");
}

void
recover(const char *args, struct mbr *mbr)
{
	FILE			*fp;

	if (args == NULL) {
		recover_disk_gpt();
		return;
	}

	fp = fopen(args, "r");
	if (fp == NULL)
		err(1, "fopen(%s)", args);

	recover_file_gpt(fp);
	if (gh.gh_sig != GPTSIGNATURE) {
		rewind(fp);
		recover_file_mbr(fp, mbr);
	}

	fclose(fp);
}
