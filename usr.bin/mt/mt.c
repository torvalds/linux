/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *	The Regents of the University of California.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2013, 2014, 2015 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Ken Merry           (Spectra Logic Corporation)
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1980, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)mt.c	8.2 (Berkeley) 5/4/95";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * mt --
 *   magnetic tape manipulation program
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/queue.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <bsdxml.h>
#include <mtlib.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_sa.h>

/* the appropriate sections of <sys/mtio.h> are also #ifdef'd for FreeBSD */
/* c_flags */
#define NEED_2ARGS	0x01
#define ZERO_ALLOWED	0x02
#define IS_DENSITY	0x04
#define DISABLE_THIS	0x08
#define IS_COMP		0x10
#define	USE_GETOPT	0x20

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX
#define	MAX(a, b) (a > b) ? a : b
#endif
#define MT_PLURAL(a) (a == 1) ? "" : "s"

typedef enum {
	MT_CMD_NONE	= MTLOAD + 1,
	MT_CMD_PROTECT,
	MT_CMD_GETDENSITY
} mt_commands;

static const struct commands {
	const char *c_name;
	unsigned long c_code;
	int c_ronly;
	int c_flags;
} com[] = {
	{ "bsf",	MTBSF,	1, 0 },
	{ "bsr",	MTBSR,	1, 0 },
	/* XXX FreeBSD considered "eof" dangerous, since it's being
	   confused with "eom" (and is an alias for "weof" anyway) */
	{ "eof",	MTWEOF, 0, DISABLE_THIS },
	{ "fsf",	MTFSF,	1, 0 },
	{ "fsr",	MTFSR,	1, 0 },
	{ "offline",	MTOFFL,	1, 0 },
	{ "load",	MTLOAD, 1, 0 },
	{ "rewind",	MTREW,	1, 0 },
	{ "rewoffl",	MTOFFL,	1, 0 },
	{ "ostatus",	MTNOP,	1, 0 },
	{ "weof",	MTWEOF,	0, ZERO_ALLOWED },
	{ "weofi",	MTWEOFI, 0, ZERO_ALLOWED },
	{ "erase",	MTERASE, 0, ZERO_ALLOWED},
	{ "blocksize",	MTSETBSIZ, 0, NEED_2ARGS|ZERO_ALLOWED },
	{ "density",	MTSETDNSTY, 0, NEED_2ARGS|ZERO_ALLOWED|IS_DENSITY },
	{ "eom",	MTEOD, 1, 0 },
	{ "eod",	MTEOD, 1, 0 },
	{ "smk",	MTWSS, 0, 0 },
	{ "wss",	MTWSS, 0, 0 },
	{ "fss",	MTFSS, 1, 0 },
	{ "bss",	MTBSS, 1, 0 },
	{ "comp",	MTCOMP, 0, NEED_2ARGS|ZERO_ALLOWED|IS_COMP },
	{ "retension",	MTRETENS, 1, 0 },
	{ "rdhpos",     MTIOCRDHPOS,  0, 0 },
	{ "rdspos",     MTIOCRDSPOS,  0, 0 },
	{ "sethpos",    MTIOCHLOCATE, 0, NEED_2ARGS|ZERO_ALLOWED },
	{ "setspos",    MTIOCSLOCATE, 0, NEED_2ARGS|ZERO_ALLOWED },
	{ "errstat",	MTIOCERRSTAT, 0, 0 },
	{ "setmodel",	MTIOCSETEOTMODEL, 0, NEED_2ARGS|ZERO_ALLOWED },
	{ "seteotmodel",	MTIOCSETEOTMODEL, 0, NEED_2ARGS|ZERO_ALLOWED },
	{ "getmodel",	MTIOCGETEOTMODEL, 0, 0 },
	{ "geteotmodel",	MTIOCGETEOTMODEL, 0, 0 },
	{ "rblim", 	MTIOCRBLIM, 0, 0},
	{ "getdensity",	MT_CMD_GETDENSITY, 0, USE_GETOPT},
	{ "status",	MTIOCEXTGET, 0, USE_GETOPT },
	{ "locate",	MTIOCEXTLOCATE, 0, USE_GETOPT },
	{ "param",	MTIOCPARAMGET, 0, USE_GETOPT },
	{ "protect", 	MT_CMD_PROTECT, 0, USE_GETOPT },
	{ NULL, 0, 0, 0 }
};


static const char *getblksiz(int);
static void printreg(const char *, u_int, const char *);
static void status(struct mtget *);
static void usage(void);
const char *get_driver_state_str(int dsreg);
static void st_status (struct mtget *);
static int mt_locate(int argc, char **argv, int mtfd, const char *tape);
static int nstatus_print(int argc, char **argv, char *xml_str,
			 struct mt_status_data *status_data);
static int mt_xml_cmd(unsigned long cmd, int argc, char **argv, int mtfd,
		      const char *tape);
static int mt_print_density_entry(struct mt_status_entry *density_root, int indent);
static int mt_print_density_report(struct mt_status_entry *report_root, int indent);
static int mt_print_density(struct mt_status_entry *density_root, int indent);
static int mt_getdensity(int argc, char **argv, char *xml_str,
			 struct mt_status_data *status_data);
static int mt_set_param(int mtfd, struct mt_status_data *status_data,
			char *param_name, char *param_value);
static int mt_protect(int argc, char **argv, int mtfd,
		      struct mt_status_data *status_data);
static int mt_param(int argc, char **argv, int mtfd, char *xml_str,
		    struct mt_status_data *status_data);
static const char *denstostring (int d);
static u_int32_t stringtocomp(const char *s);
static const char *comptostring(u_int32_t comp);
static void warn_eof(void);

int
main(int argc, char *argv[])
{
	const struct commands *comp;
	struct mtget mt_status;
	struct mtop mt_com;
	int ch, len, mtfd;
	const char *p, *tape;

	bzero(&mt_com, sizeof(mt_com));
	
	if ((tape = getenv("TAPE")) == NULL)
		tape = DEFTAPE;

	while ((ch = getopt(argc, argv, "f:t:")) != -1)
		switch(ch) {
		case 'f':
		case 't':
			tape = optarg;
			break;
		case '?':
			usage();
			break;
		default:
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	len = strlen(p = *argv++);
	for (comp = com;; comp++) {
		if (comp->c_name == NULL)
			errx(1, "%s: unknown command", p);
		if (strncmp(p, comp->c_name, len) == 0)
			break;
	}
	if((comp->c_flags & NEED_2ARGS) && argc != 2)
		usage();
	if(comp->c_flags & DISABLE_THIS) {
		warn_eof();
	}
	if (comp->c_flags & USE_GETOPT) {
		argc--;
		optind = 0;
	}

	if ((mtfd = open(tape, comp->c_ronly ? O_RDONLY : O_RDWR)) < 0)
		err(1, "%s", tape);
	if (comp->c_code != MTNOP) {
		mt_com.mt_op = comp->c_code;
		if (*argv) {
			if (!isdigit(**argv) &&
			    (comp->c_flags & IS_DENSITY)) {
				const char *dcanon;
				mt_com.mt_count = mt_density_num(*argv);
				if (mt_com.mt_count == 0)
					errx(1, "%s: unknown density", *argv);
				dcanon = denstostring(mt_com.mt_count);
				if (strcmp(dcanon, *argv) != 0)
					printf(
					"Using \"%s\" as an alias for %s\n",
					       *argv, dcanon);
				p = "";
			} else if (!isdigit(**argv) &&
				   (comp->c_flags & IS_COMP)) {

				mt_com.mt_count = stringtocomp(*argv);
				if ((u_int32_t)mt_com.mt_count == 0xf0f0f0f0)
					errx(1, "%s: unknown compression",
					     *argv);
				p = "";
			} else if ((comp->c_flags & USE_GETOPT) == 0) {
				char *q;
				/* allow for hex numbers; useful for density */
				mt_com.mt_count = strtol(*argv, &q, 0);
				p = q;
			}
			if (((comp->c_flags & USE_GETOPT) == 0)
			 && (((mt_com.mt_count <=
			     ((comp->c_flags & ZERO_ALLOWED)? -1: 0))
			   && ((comp->c_flags & IS_COMP) == 0))
			  || *p))
				errx(1, "%s: illegal count", *argv);
		}
		else
			mt_com.mt_count = 1;
		switch (comp->c_code) {
		case MTIOCERRSTAT:
		{
			unsigned int i;
			union mterrstat umn;
			struct scsi_tape_errors *s = &umn.scsi_errstat;

			if (ioctl(mtfd, comp->c_code, (caddr_t)&umn) < 0)
				err(2, "%s", tape);
			(void)printf("Last I/O Residual: %u\n", s->io_resid);
			(void)printf(" Last I/O Command:");
			for (i = 0; i < sizeof (s->io_cdb); i++)
				(void)printf(" %02X", s->io_cdb[i]);
			(void)printf("\n");
			(void)printf("   Last I/O Sense:\n\n\t");
			for (i = 0; i < sizeof (s->io_sense); i++) {
				(void)printf(" %02X", s->io_sense[i]);
				if (((i + 1) & 0xf) == 0) {
					(void)printf("\n\t");
				}
			}
			(void)printf("\n");
			(void)printf("Last Control Residual: %u\n",
			    s->ctl_resid);
			(void)printf(" Last Control Command:");
			for (i = 0; i < sizeof (s->ctl_cdb); i++)
				(void)printf(" %02X", s->ctl_cdb[i]);
			(void)printf("\n");
			(void)printf("   Last Control Sense:\n\n\t");
			for (i = 0; i < sizeof (s->ctl_sense); i++) {
				(void)printf(" %02X", s->ctl_sense[i]);
				if (((i + 1) & 0xf) == 0) {
					(void)printf("\n\t");
				}
			}
			(void)printf("\n\n");
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCRDHPOS:
		case MTIOCRDSPOS:
		{
			u_int32_t block;
			if (ioctl(mtfd, comp->c_code, (caddr_t)&block) < 0)
				err(2, "%s", tape);
			(void)printf("%s: %s block location %u\n", tape,
			    (comp->c_code == MTIOCRDHPOS)? "hardware" :
			    "logical", block);
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCSLOCATE:
		case MTIOCHLOCATE:
		{
			u_int32_t block = (u_int32_t)mt_com.mt_count;
			if (ioctl(mtfd, comp->c_code, (caddr_t)&block) < 0)
				err(2, "%s", tape);
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCGETEOTMODEL:
		{
			u_int32_t om;
			if (ioctl(mtfd, MTIOCGETEOTMODEL, (caddr_t)&om) < 0)
				err(2, "%s", tape);
			(void)printf("%s: the model is %u filemar%s at EOT\n",
			    tape, om, (om > 1)? "ks" : "k");
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCSETEOTMODEL:
		{
			u_int32_t om, nm = (u_int32_t)mt_com.mt_count;
			if (ioctl(mtfd, MTIOCGETEOTMODEL, (caddr_t)&om) < 0)
				err(2, "%s", tape);
			if (ioctl(mtfd, comp->c_code, (caddr_t)&nm) < 0)
				err(2, "%s", tape);
			(void)printf("%s: old model was %u filemar%s at EOT\n",
			    tape, om, (om > 1)? "ks" : "k");
			(void)printf("%s: new model  is %u filemar%s at EOT\n",
			    tape, nm, (nm > 1)? "ks" : "k");
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCRBLIM:
		{
			struct mtrblim rblim;

			bzero(&rblim, sizeof(rblim));

			if (ioctl(mtfd, MTIOCRBLIM, (caddr_t)&rblim) < 0)
				err(2, "%s", tape);
			(void)printf("%s:\n"
			    "    min blocksize %u byte%s\n"
			    "    max blocksize %u byte%s\n"
			    "    granularity %u byte%s\n",
			    tape, rblim.min_block_length,
			    MT_PLURAL(rblim.min_block_length),
			    rblim.max_block_length,
			    MT_PLURAL(rblim.max_block_length),
			    (1 << rblim.granularity),
			    MT_PLURAL((1 << rblim.granularity)));
			exit(0);
			/* NOTREACHED */
		}
		case MTIOCPARAMGET:
		case MTIOCEXTGET:
		case MT_CMD_PROTECT:
		case MT_CMD_GETDENSITY:
		{
			int retval = 0;

			retval = mt_xml_cmd(comp->c_code, argc, argv, mtfd,
			    tape);

			exit(retval);
		}
		case MTIOCEXTLOCATE:
		{
			int retval = 0;

			retval = mt_locate(argc, argv, mtfd, tape);

			exit(retval);
		}
		default:
			break;
		}
		if (ioctl(mtfd, MTIOCTOP, &mt_com) < 0)
			err(1, "%s: %s", tape, comp->c_name);
	} else {
		if (ioctl(mtfd, MTIOCGET, &mt_status) < 0)
			err(1, NULL);
		status(&mt_status);
	}
	exit(0);
	/* NOTREACHED */
}

static const struct tape_desc {
	short	t_type;		/* type of magtape device */
	const char *t_name;	/* printing name */
	const char *t_dsbits;	/* "drive status" register */
	const char *t_erbits;	/* "error" register */
} tapes[] = {
	{ MT_ISAR,	"SCSI tape drive", 0,		0 },
	{ 0, NULL, 0, 0 }
};

/*
 * Interpret the status buffer returned
 */
static void
status(struct mtget *bp)
{
	const struct tape_desc *mt;

	for (mt = tapes;; mt++) {
		if (mt->t_type == 0) {
			(void)printf("%d: unknown tape drive type\n",
			    bp->mt_type);
			return;
		}
		if (mt->t_type == bp->mt_type)
			break;
	}
	if(mt->t_type == MT_ISAR)
		st_status(bp);
	else {
		(void)printf("%s tape drive, residual=%d\n", 
		    mt->t_name, bp->mt_resid);
		printreg("ds", (unsigned short)bp->mt_dsreg, mt->t_dsbits);
		printreg("\ner", (unsigned short)bp->mt_erreg, mt->t_erbits);
		(void)putchar('\n');
	}
}

/*
 * Print a register a la the %b format of the kernel's printf.
 */
static void
printreg(const char *s, u_int v, const char *bits)
{
	int i, any = 0;
	char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);
	if (!bits)
		return;
	bits++;
	if (v && bits) {
		putchar('<');
		while ((i = *bits++)) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: mt [-f device] command [count]\n");
	exit(1);
}

static const struct compression_types {
	u_int32_t	comp_number;
	const char 	*name;
} comp_types[] = {
	{ 0x00, "none" },
	{ 0x00, "off" },
	{ 0x10, "IDRC" },
	{ 0x20, "DCLZ" },
	{ 0xffffffff, "enable" },
	{ 0xffffffff, "on" },
	{ 0xf0f0f0f0, NULL}
};

static const char *
denstostring(int d)
{
	static char buf[20];
	const char *name = mt_density_name(d);

	if (name == NULL)
		sprintf(buf, "0x%02x", d);
	else 
		sprintf(buf, "0x%02x:%s", d, name);
	return buf;
}

static const char *
getblksiz(int bs)
{
	static char buf[25];
	if (bs == 0)
		return "variable";
	else {
		sprintf(buf, "%d bytes", bs);
		return buf;
	}
}

static const char *
comptostring(u_int32_t comp)
{
	static char buf[20];
	const struct compression_types *ct;

	if (comp == MT_COMP_DISABLED)
		return "disabled";
	else if (comp == MT_COMP_UNSUPP)
		return "unsupported";

	for (ct = comp_types; ct->name; ct++)
		if (ct->comp_number == comp)
			break;

	if (ct->comp_number == 0xf0f0f0f0) {
		sprintf(buf, "0x%x", comp);
		return(buf);
	} else
		return(ct->name);
}

static u_int32_t
stringtocomp(const char *s)
{
	const struct compression_types *ct;
	size_t l = strlen(s);

	for (ct = comp_types; ct->name; ct++)
		if (strncasecmp(ct->name, s, l) == 0)
			break;

	return(ct->comp_number);
}

static struct driver_state {
	int dsreg;
	const char *desc;
} driver_states[] = {
	{ MTIO_DSREG_REST, "at rest" },
	{ MTIO_DSREG_RBSY, "Communicating with drive" },
	{ MTIO_DSREG_WR, "Writing" },
	{ MTIO_DSREG_FMK, "Writing Filemarks" },
	{ MTIO_DSREG_ZER, "Erasing" },
	{ MTIO_DSREG_RD, "Reading" },
	{ MTIO_DSREG_FWD, "Spacing Forward" },
	{ MTIO_DSREG_REV, "Spacing Reverse" },
	{ MTIO_DSREG_POS, "Hardware Positioning (direction unknown)" },
	{ MTIO_DSREG_REW, "Rewinding" },
	{ MTIO_DSREG_TEN, "Retensioning" },
	{ MTIO_DSREG_UNL, "Unloading" },
	{ MTIO_DSREG_LD, "Loading" },
};

const char *
get_driver_state_str(int dsreg)
{
	unsigned int i;

	for (i = 0; i < (sizeof(driver_states)/sizeof(driver_states[0])); i++) {
		if (driver_states[i].dsreg == dsreg)
			return (driver_states[i].desc);
	}

	return (NULL);
}

static void
st_status(struct mtget *bp)
{
	printf("Mode      Density              Blocksize      bpi      "
	       "Compression\n"
	       "Current:  %-17s    %-12s   %-7d  %s\n"
	       "---------available modes---------\n"
	       "0:        %-17s    %-12s   %-7d  %s\n"
	       "1:        %-17s    %-12s   %-7d  %s\n"
	       "2:        %-17s    %-12s   %-7d  %s\n"
	       "3:        %-17s    %-12s   %-7d  %s\n",
	       denstostring(bp->mt_density), getblksiz(bp->mt_blksiz),
	       mt_density_bp(bp->mt_density, TRUE), comptostring(bp->mt_comp),
	       denstostring(bp->mt_density0), getblksiz(bp->mt_blksiz0),
	       mt_density_bp(bp->mt_density0, TRUE), comptostring(bp->mt_comp0),
	       denstostring(bp->mt_density1), getblksiz(bp->mt_blksiz1),
	       mt_density_bp(bp->mt_density1, TRUE), comptostring(bp->mt_comp1),
	       denstostring(bp->mt_density2), getblksiz(bp->mt_blksiz2),
	       mt_density_bp(bp->mt_density2, TRUE), comptostring(bp->mt_comp2),
	       denstostring(bp->mt_density3), getblksiz(bp->mt_blksiz3),
	       mt_density_bp(bp->mt_density3, TRUE), comptostring(bp->mt_comp3));

	if (bp->mt_dsreg != MTIO_DSREG_NIL) {
		const char sfmt[] = "Current Driver State: %s.\n";
		printf("---------------------------------\n");
		const char *state_str;

		state_str = get_driver_state_str(bp->mt_dsreg);
		if (state_str == NULL) {
			char foo[32];
			(void) sprintf(foo, "Unknown state 0x%x", bp->mt_dsreg);
			printf(sfmt, foo);
		} else {
			printf(sfmt, state_str);
		}
	}
	if (bp->mt_resid == 0 && bp->mt_fileno == (daddr_t) -1 &&
	    bp->mt_blkno == (daddr_t) -1)
		return;
	printf("---------------------------------\n");
	printf("File Number: %d\tRecord Number: %d\tResidual Count %d\n",
	    bp->mt_fileno, bp->mt_blkno, bp->mt_resid);
}

static int
mt_locate(int argc, char **argv, int mtfd, const char *tape)
{
	struct mtlocate mtl;
	uint64_t logical_id = 0;
	mt_locate_dest_type dest_type = MT_LOCATE_DEST_FILE;
	int eod = 0, explicit = 0, immediate = 0;
	int64_t partition = 0;
	int block_addr_set = 0, partition_set = 0, file_set = 0, set_set = 0;
	int c, retval;

	retval = 0;
	bzero(&mtl, sizeof(mtl));

	while ((c = getopt(argc, argv, "b:eEf:ip:s:")) != -1) {
		switch (c) {
		case 'b':
			/* Block address */
			logical_id = strtoull(optarg, NULL, 0);
			dest_type = MT_LOCATE_DEST_OBJECT;
			block_addr_set = 1;
			break;
		case 'e':
			/* end of data */
			eod = 1;
			dest_type = MT_LOCATE_DEST_EOD;
			break;
		case 'E':
			/*
			 * XXX KDM explicit address mode.  Should we even
			 * allow this, since the driver doesn't operate in
			 * explicit address mode?
			 */
			explicit = 1;
			break;
		case 'f':
			/* file number */
			logical_id = strtoull(optarg, NULL, 0);
			dest_type = MT_LOCATE_DEST_FILE;
			file_set = 1;
			break;
		case 'i':
			/*
			 * Immediate address mode.  XXX KDM do we want to
			 * implement this?  The other commands in the
			 * tape driver will need to be able to handle this.
			 */
			immediate = 1;
			break;
		case 'p':
			/*
			 * Change partition to the given partition.
			 */
			partition = strtol(optarg, NULL, 0);
			partition_set = 1;
			break;
		case 's':
			/* Go to the given set mark */
			logical_id = strtoull(optarg, NULL, 0);
			dest_type = MT_LOCATE_DEST_SET;
			set_set = 1;
			break;
		default:
			break;
		}
	}

	/*
	 * These options are mutually exclusive.  The user may only specify
	 * one.
	 */
	if ((block_addr_set + file_set + eod + set_set) != 1)
		errx(1, "You must specify only one of -b, -f, -e, or -s");

	mtl.dest_type = dest_type;
	switch (dest_type) {
	case MT_LOCATE_DEST_OBJECT:
	case MT_LOCATE_DEST_FILE:
	case MT_LOCATE_DEST_SET:
		mtl.logical_id = logical_id;
		break;
	case MT_LOCATE_DEST_EOD:
		break;
	}

	if (immediate != 0)
		mtl.flags |= MT_LOCATE_FLAG_IMMED;

	if (partition_set != 0) {
		mtl.flags |= MT_LOCATE_FLAG_CHANGE_PART;
		mtl.partition = partition;
	}

	if (explicit != 0)
		mtl.block_address_mode = MT_LOCATE_BAM_EXPLICIT;
	else
		mtl.block_address_mode = MT_LOCATE_BAM_IMPLICIT;

	if (ioctl(mtfd, MTIOCEXTLOCATE, &mtl) == -1)
		err(1, "MTIOCEXTLOCATE ioctl failed on %s", tape);

	return (retval);
}

typedef enum {
	MT_PERIPH_NAME			= 0,
	MT_UNIT_NUMBER 			= 1,
	MT_VENDOR			= 2,
	MT_PRODUCT			= 3,
	MT_REVISION			= 4,
	MT_COMPRESSION_SUPPORTED	= 5,
	MT_COMPRESSION_ENABLED		= 6,
	MT_COMPRESSION_ALGORITHM	= 7,
	MT_MEDIA_DENSITY		= 8,
	MT_MEDIA_BLOCKSIZE		= 9,
	MT_CALCULATED_FILENO		= 10,
	MT_CALCULATED_REL_BLKNO		= 11,
	MT_REPORTED_FILENO		= 12,
	MT_REPORTED_BLKNO		= 13,
	MT_PARTITION			= 14,
	MT_BOP				= 15,
	MT_EOP				= 16,
	MT_BPEW				= 17,
	MT_DSREG			= 18,
	MT_RESID			= 19,
	MT_FIXED_MODE			= 20,
	MT_SERIAL_NUM			= 21,
	MT_MAXIO			= 22,
	MT_CPI_MAXIO			= 23,
	MT_MAX_BLK			= 24,
	MT_MIN_BLK			= 25,
	MT_BLK_GRAN			= 26,
	MT_MAX_EFF_IOSIZE		= 27
} status_item_index;

static struct mt_status_items {
	const char *name;
	struct mt_status_entry *entry;
} req_status_items[] = {
	{ "periph_name", NULL },
	{ "unit_number", NULL },
	{ "vendor", NULL },
	{ "product", NULL },
	{ "revision", NULL },
	{ "compression_supported", NULL },
	{ "compression_enabled", NULL },
	{ "compression_algorithm", NULL },
	{ "media_density", NULL },
	{ "media_blocksize", NULL },
	{ "calculated_fileno", NULL },
	{ "calculated_rel_blkno", NULL },
	{ "reported_fileno", NULL },
	{ "reported_blkno", NULL },
	{ "partition", NULL },
	{ "bop", NULL },
	{ "eop", NULL },
	{ "bpew", NULL },
	{ "dsreg", NULL },
	{ "residual", NULL },
	{ "fixed_mode", NULL },
	{ "serial_num", NULL },
	{ "maxio", NULL },
	{ "cpi_maxio", NULL },
	{ "max_blk", NULL },
	{ "min_blk", NULL },
	{ "blk_gran", NULL },
	{ "max_effective_iosize", NULL }
};

int
nstatus_print(int argc, char **argv, char *xml_str,
	      struct mt_status_data *status_data)
{
	unsigned int i;
	int64_t calculated_fileno, calculated_rel_blkno;
	int64_t rep_fileno, rep_blkno, partition, resid;
	char block_str[32];
	const char *dens_str;
	int dsreg, bop, eop, bpew;
	int xml_dump = 0;
	size_t dens_len;
	unsigned int field_width;
	int verbose = 0;
	int c;

	while ((c = getopt(argc, argv, "xv")) != -1) {
		switch (c) {
		case 'x':
			xml_dump = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			break;
		}
	}

	if (xml_dump != 0) {
		printf("%s", xml_str);
		return (0);
	}

	for (i = 0; i < (sizeof(req_status_items)/sizeof(req_status_items[0]));
	     i++) {
		char *name;

		name = __DECONST(char *, req_status_items[i].name);
		req_status_items[i].entry = mt_status_entry_find(status_data,
		    name);
		if (req_status_items[i].entry == NULL) {
			errx(1, "Cannot find status entry %s",
			    req_status_items[i].name);
		}
	}

	printf("Drive: %s%ju: <%s %s %s> Serial Number: %s\n",
	       req_status_items[MT_PERIPH_NAME].entry->value,
	       (uintmax_t)req_status_items[MT_UNIT_NUMBER].entry->value_unsigned,
	       req_status_items[MT_VENDOR].entry->value,
	       req_status_items[MT_PRODUCT].entry->value,
	       req_status_items[MT_REVISION].entry->value,
	       (req_status_items[MT_SERIAL_NUM].entry->value) ? 
	       req_status_items[MT_SERIAL_NUM].entry->value : "none");
	printf("---------------------------------\n");

	/*
	 * We check to see whether we're in fixed mode or not, and don't
	 * just believe the blocksize.  If the SILI bit is turned on, the
	 * blocksize will be set to 4, even though we're doing variable
	 * length (well, multiples of 4) blocks.
	 */
	if (req_status_items[MT_FIXED_MODE].entry->value_signed == 0)
		snprintf(block_str, sizeof(block_str), "variable");
	else
		snprintf(block_str, sizeof(block_str), "%s",
		    getblksiz(req_status_items[
			      MT_MEDIA_BLOCKSIZE].entry->value_unsigned));

	dens_str = denstostring(req_status_items[
	    MT_MEDIA_DENSITY].entry->value_unsigned);
	if (dens_str == NULL)
		dens_len = 0;
	else
		dens_len = strlen(dens_str);
	field_width = MAX(dens_len, 17);
	printf("Mode      %-*s    Blocksize      bpi      Compression\n"
	       "Current:  %-*s    %-12s   %-7d  ",
	       field_width, "Density", field_width, dens_str, block_str,
	       mt_density_bp(req_status_items[
	       MT_MEDIA_DENSITY].entry->value_unsigned, TRUE));

	if (req_status_items[MT_COMPRESSION_SUPPORTED].entry->value_signed == 0)
		printf("unsupported\n");
	else if (req_status_items[
		 MT_COMPRESSION_ENABLED].entry->value_signed == 0)
		printf("disabled\n");
	else {
		printf("enabled (%s)\n",
		       comptostring(req_status_items[
		       MT_COMPRESSION_ALGORITHM].entry->value_unsigned));
	}

	dsreg = req_status_items[MT_DSREG].entry->value_signed;
	if (dsreg != MTIO_DSREG_NIL) {
		const char sfmt[] = "Current Driver State: %s.\n";
		printf("---------------------------------\n");
		const char *state_str;

		state_str = get_driver_state_str(dsreg);
		if (state_str == NULL) {
			char foo[32];
			(void) sprintf(foo, "Unknown state 0x%x", dsreg);
			printf(sfmt, foo);
		} else {
			printf(sfmt, state_str);
		}
	}
	resid = req_status_items[MT_RESID].entry->value_signed;
	calculated_fileno = req_status_items[
	    MT_CALCULATED_FILENO].entry->value_signed;
	calculated_rel_blkno = req_status_items[
	    MT_CALCULATED_REL_BLKNO].entry->value_signed;
	rep_fileno = req_status_items[
	    MT_REPORTED_FILENO].entry->value_signed;
	rep_blkno = req_status_items[
	    MT_REPORTED_BLKNO].entry->value_signed;
	bop = req_status_items[MT_BOP].entry->value_signed;
	eop = req_status_items[MT_EOP].entry->value_signed;
	bpew = req_status_items[MT_BPEW].entry->value_signed;
	partition = req_status_items[MT_PARTITION].entry->value_signed;

	printf("---------------------------------\n");
	printf("Partition: %3jd      Calc File Number: %3jd "
	       "    Calc Record Number: %jd\n"
	       "Residual:  %3jd  Reported File Number: %3jd "
	       "Reported Record Number: %jd\n", partition, calculated_fileno,
	       calculated_rel_blkno, resid, rep_fileno, rep_blkno);

	printf("Flags: ");
	if (bop > 0 || eop > 0 || bpew > 0) {
		int need_comma = 0;

		if (bop > 0) {
			printf("BOP");
			need_comma = 1;
		}
		if (eop > 0) {
			if (need_comma != 0)
				printf(",");
			printf("EOP");
			need_comma = 1;
		}
		if (bpew > 0) {
			if (need_comma != 0)
				printf(",");
			printf("BPEW");
			need_comma = 1;
		}
	} else {
		printf("None");
	}
	printf("\n");
	if (verbose != 0) {
		printf("---------------------------------\n");
		printf("Tape I/O parameters:\n");
		for (i = MT_MAXIO; i <= MT_MAX_EFF_IOSIZE; i++) {
			printf("  %s (%s): %ju bytes\n",
			    req_status_items[i].entry->desc,
			    req_status_items[i].name,
			    req_status_items[i].entry->value_unsigned);
		}
	}

	return (0);
}

int
mt_xml_cmd(unsigned long cmd, int argc, char **argv, int mtfd, const char *tape)
{
	struct mt_status_data status_data;
#if 0
	struct mt_status_entry *entry;
#endif
	char *xml_str;
	int retval;
	unsigned long ioctl_cmd;

	switch (cmd) {
	case MT_CMD_PROTECT:
	case MTIOCPARAMGET:
		ioctl_cmd = MTIOCPARAMGET;
		break;
	default:
		ioctl_cmd = MTIOCEXTGET;
		break;
	}

	retval = mt_get_xml_str(mtfd, ioctl_cmd, &xml_str);
	if (retval != 0)
		err(1, "Couldn't get mt XML string");

	retval = mt_get_status(xml_str, &status_data);
	if (retval != XML_STATUS_OK) {
		warn("Couldn't get mt status for %s", tape);
		goto bailout;
	}

	/*
	 * This gets set if there are memory allocation or other errors in
	 * our parsing of the XML. 
	 */
	if (status_data.error != 0) {
		warnx("%s", status_data.error_str);
		retval = 1;
		goto bailout;
	}
#if 0
	STAILQ_FOREACH(entry, &status_data.entries, links)
		mt_status_tree_print(entry, 0, NULL);
#endif

	switch (cmd) {
	case MTIOCEXTGET:
		retval = nstatus_print(argc, argv, xml_str, &status_data);
		break;
	case MTIOCPARAMGET:
		retval = mt_param(argc, argv, mtfd, xml_str, &status_data);
		break;
	case MT_CMD_PROTECT:
		retval = mt_protect(argc, argv, mtfd, &status_data);
		break;
	case MT_CMD_GETDENSITY:
		retval = mt_getdensity(argc, argv, xml_str, &status_data);
		break;
	}

bailout:
	if (xml_str != NULL)
		free(xml_str);

	mt_status_free(&status_data);

	return (retval);
}

static int
mt_set_param(int mtfd, struct mt_status_data *status_data, char *param_name,
    char *param_value)
{
	struct mt_status_entry *entry;
	struct mtparamset param_set;

	entry = mt_status_entry_find(status_data,
	    __DECONST(char *, "mtparamget"));
	if (entry == NULL)
		errx(1, "Cannot find parameter root node");

	bzero(&param_set, sizeof(param_set));
	entry = mt_entry_find(entry, param_name);
	if (entry == NULL)
		errx(1, "Unknown parameter name \"%s\"", param_name);

	strlcpy(param_set.value_name, param_name, sizeof(param_set.value_name));

	switch (entry->var_type) {
	case MT_TYPE_INT:
		param_set.value.value_signed = strtoll(param_value, NULL, 0);
		param_set.value_type = MT_PARAM_SET_SIGNED;
		param_set.value_len = entry->size;
		break;
	case MT_TYPE_UINT:
		param_set.value.value_unsigned = strtoull(param_value, NULL, 0);
		param_set.value_type = MT_PARAM_SET_UNSIGNED;
		param_set.value_len = entry->size;
		break;
	case MT_TYPE_STRING: {
		size_t param_len;

		param_len = strlen(param_value) + 1;
		if (param_len > sizeof(param_set.value.value_fixed_str)) {
			param_set.value_type = MT_PARAM_SET_VAR_STR;
			param_set.value.value_var_str = param_value;
		} else {
			param_set.value_type = MT_PARAM_SET_FIXED_STR;
			strlcpy(param_set.value.value_fixed_str, param_value,
			    sizeof(param_set.value.value_fixed_str));
		}
		param_set.value_len = param_len;
		break;
	}
	default:
		errx(1, "Unknown parameter type %d for %s", entry->var_type,
		    param_name);
		break;
	}

	if (ioctl(mtfd, MTIOCPARAMSET, &param_set) == -1)
		err(1, "MTIOCPARAMSET");

	if (param_set.status != MT_PARAM_STATUS_OK)
		errx(1, "Failed to set %s: %s", param_name,
		    param_set.error_str);

	return (0);
}


typedef enum {
	MT_PP_LBP_R,
	MT_PP_LBP_W,
	MT_PP_RBDP,
	MT_PP_PI_LENGTH,
	MT_PP_PROT_METHOD
} mt_protect_param;

static struct mt_protect_info {
	const char *name;
	struct mt_status_entry *entry;
	uint32_t value;
} mt_protect_list[] = {
	{ "lbp_r", NULL, 0 },
	{ "lbp_w", NULL, 0 },
	{ "rbdp", NULL, 0 },
	{ "pi_length", NULL, 0 },
	{ "prot_method", NULL, 0 }
};

#define	MT_NUM_PROTECT_PARAMS	(sizeof(mt_protect_list)/sizeof(mt_protect_list[0]))

#define	MT_PROT_NAME	"protection"

static int
mt_protect(int argc, char **argv, int mtfd, struct mt_status_data *status_data)
{
	int retval = 0;
	int do_enable = 0, do_disable = 0, do_list = 0;
	int rbdp_set = 0, lbp_w_set = 0, lbp_r_set = 0;
	int prot_method_set = 0, pi_length_set = 0;
	int verbose = 0;
	uint32_t rbdp = 0, lbp_w = 0, lbp_r = 0;
	uint32_t prot_method = 0, pi_length = 0;
	struct mt_status_entry *prot_entry, *supported_entry;
	struct mt_status_entry *entry;
	struct mtparamset params[MT_NUM_PROTECT_PARAMS];
	struct mtsetlist param_list;
	unsigned int i;
	int c;

	while ((c = getopt(argc, argv, "b:delL:m:r:vw:")) != -1) {
		switch (c) {
		case 'b':
			rbdp_set = 1;
			rbdp = strtoul(optarg, NULL, 0);
			if ((rbdp != 0) && (rbdp != 1))
				errx(1, "valid values for -b are 0 and 1");
			break;
		case 'd':
			do_disable = 1;
			break;
		case 'e':
			do_enable = 1;
			break;
		case 'l':
			do_list = 1;
			break;
		case 'L':
			pi_length_set = 1;
			pi_length = strtoul(optarg, NULL, 0);
			if (pi_length > SA_CTRL_DP_PI_LENGTH_MASK)
				errx(1, "PI length %u > maximum %u",
				    pi_length, SA_CTRL_DP_PI_LENGTH_MASK);
			break;
		case 'm':
			prot_method_set = 1;
			prot_method = strtoul(optarg, NULL, 0);
			if (prot_method > SA_CTRL_DP_METHOD_MAX)
				errx(1, "Method %u > maximum %u",
				    prot_method, SA_CTRL_DP_METHOD_MAX);
			break;
		case 'r':
			lbp_r_set = 1;
			lbp_r = strtoul(optarg, NULL, 0);
			if ((lbp_r != 0) && (lbp_r != 1))
				errx(1, "valid values for -r are 0 and 1");
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			lbp_w_set = 1;
			lbp_w = strtoul(optarg, NULL, 0);
			if ((lbp_w != 0) && (lbp_r != 1))
				errx(1, "valid values for -r are 0 and 1");
			break;
		default:
			break;
		}
	}

	if ((rbdp_set + do_disable + do_enable + do_list + pi_length_set +
	    prot_method_set + lbp_r_set + lbp_w_set) == 0)
		errx(1, "Need an argument for protect");

	if ((do_disable + do_enable + do_list) != 1)
		errx(1, "You must specify only one of -e, -d or -l");

	if (do_list != 0) {
		retval = mt_protect_print(status_data, verbose);
		goto bailout;
	}
	if (do_enable != 0) {
		/*
		 * Enable protection, but allow the user to override
		 * settings if he doesn't want everything turned on.
		 */
		if (rbdp_set == 0)
			rbdp = 1;
		if (lbp_w_set == 0)
			lbp_w = 1;
		if (lbp_r_set == 0)
			lbp_r = 1;
		/*
		 * If the user doesn't override it, we default to enabling
		 * Reed-Solomon checkums.
		 */
		if (prot_method_set == 0)
			prot_method = SA_CTRL_DP_REED_SOLOMON;
		if (pi_length_set == 0)
			pi_length = SA_CTRL_DP_RS_LENGTH;
	} else if (do_disable != 0) {
		/*
		 * If the user wants to disable protection, we ignore any
		 * other parameters he has set.  Everything gets set to 0.
		 */
		rbdp = lbp_w = lbp_r = 0;
		prot_method = pi_length = 0;
	}

	prot_entry = mt_status_entry_find(status_data,
	    __DECONST(char *, MT_PROT_NAME));
	if (prot_entry == NULL)
		errx(1, "Unable to find protection information status");

	supported_entry = mt_entry_find(prot_entry,
	    __DECONST(char *, "protection_supported"));
	if (supported_entry == NULL)
		errx(1, "Unable to find protection support information");

	if (((supported_entry->var_type == MT_TYPE_INT)
	  && (supported_entry->value_signed == 0))
	 || ((supported_entry->var_type == MT_TYPE_UINT)
	  && (supported_entry->value_unsigned == 0)))
		errx(1, "This device does not support protection information");

	mt_protect_list[MT_PP_LBP_R].value = lbp_r;
	mt_protect_list[MT_PP_LBP_W].value = lbp_w;
	mt_protect_list[MT_PP_RBDP].value = rbdp;
	mt_protect_list[MT_PP_PI_LENGTH].value = pi_length;
	mt_protect_list[MT_PP_PROT_METHOD].value = prot_method;

	bzero(&params, sizeof(params));
	bzero(&param_list, sizeof(param_list));

	/*
	 * Go through the list and make sure that we have this parameter,
	 * and that it is still an unsigned integer.  If not, we've got a
	 * problem.
	 */
	for (i = 0; i < MT_NUM_PROTECT_PARAMS; i++) {
		entry = mt_entry_find(prot_entry,
		    __DECONST(char *, mt_protect_list[i].name));
		if (entry == NULL) {
			errx(1, "Unable to find parameter %s",
			    mt_protect_list[i].name);
		}
		mt_protect_list[i].entry = entry;

		if (entry->var_type != MT_TYPE_UINT)
			errx(1, "Parameter %s is type %d, not unsigned, "
			    "cannot proceed", mt_protect_list[i].name,
			    entry->var_type);
		snprintf(params[i].value_name, sizeof(params[i].value_name),
		    "%s.%s", MT_PROT_NAME, mt_protect_list[i].name);
		/* XXX KDM unify types here */
		params[i].value_type = MT_PARAM_SET_UNSIGNED;
		params[i].value_len = sizeof(mt_protect_list[i].value);
		params[i].value.value_unsigned = mt_protect_list[i].value;
		
	}
	param_list.num_params = MT_NUM_PROTECT_PARAMS;
	param_list.param_len = sizeof(params);
	param_list.params = params;

	if (ioctl(mtfd, MTIOCSETLIST, &param_list) == -1)
		err(1, "error issuing MTIOCSETLIST ioctl");

	for (i = 0; i < MT_NUM_PROTECT_PARAMS; i++) {
		if (params[i].status != MT_PARAM_STATUS_OK) {
			warnx("%s", params[i].error_str);
			retval = 1;
		}
	}
bailout:

	return (retval);
}

static int
mt_param(int argc, char **argv, int mtfd, char *xml_str,
	 struct mt_status_data *status_data)
{
	int list = 0, do_set = 0, xml_dump = 0;
	char *param_name = NULL, *param_value = NULL;
	int retval = 0, quiet = 0;
	int c;

	while ((c = getopt(argc, argv, "lp:qs:x")) != -1) {
		switch (c) {
		case 'l':
			list = 1;
			break;
		case 'p':
			if (param_name != NULL) {
				warnx("Only one parameter name may be "
				    "specified");
				retval = 1;
				goto bailout;
			}
			param_name = strdup(optarg);
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			if (param_value != NULL) {
				warnx("Only one parameter value may be "
				    "specified");
				retval = 1;
				goto bailout;
			}
			param_value = strdup(optarg);
			do_set = 1;
			break;
		case 'x':
			xml_dump = 1;
			break;
		default:
			break;
		}
	}

	if ((list + do_set + xml_dump) != 1) {
		warnx("You must specify only one of -s, -l or -x");
		retval = 1;
		goto bailout;
	}

	if (xml_dump != 0) {
		printf("%s", xml_str);
		retval = 0;
		goto bailout;
	}

	if (do_set != 0) {
		if (param_name == NULL)
			errx(1, "You must specify -p with -s");

		retval = mt_set_param(mtfd, status_data, param_name,
		    param_value);
	} else if (list != 0)
		retval = mt_param_list(status_data, param_name, quiet);

bailout:
	free(param_name);
	free(param_value);
	return (retval);
}

int
mt_print_density_entry(struct mt_status_entry *density_root, int indent)
{
	struct mt_status_entry *entry;
	int retval = 0;

	STAILQ_FOREACH(entry, &density_root->child_entries, links) {
		if (entry->var_type == MT_TYPE_NODE) {
			retval = mt_print_density_entry(entry, indent + 2);
			if (retval != 0)
				break;
			else
				continue;
		}
		if ((strcmp(entry->entry_name, "primary_density_code") == 0)
		 || (strcmp(entry->entry_name, "secondary_density_code") == 0)
		 || (strcmp(entry->entry_name, "density_code") == 0)) {

			printf("%*s%s (%s): %s\n", indent, "", entry->desc ?
			    entry->desc : "", entry->entry_name,
			    denstostring(entry->value_unsigned));
		} else if (strcmp(entry->entry_name, "density_flags") == 0) {
			printf("%*sMedium Access: ", indent, "");
			if (entry->value_unsigned & MT_DENS_WRITE_OK) {
				printf("Read and Write\n");
			} else {
				printf("Read Only\n");
			}
			printf("%*sDefault Density: %s\n", indent, "",
			    (entry->value_unsigned & MT_DENS_DEFLT) ? "Yes" :
			    "No");
			printf("%*sDuplicate Density: %s\n", indent, "",
			    (entry->value_unsigned & MT_DENS_DUP) ? "Yes" :
			    "No");
		} else if (strcmp(entry->entry_name, "media_width") == 0) {
			printf("%*s%s (%s): %.1f mm\n", indent, "",
			    entry->desc ?  entry->desc : "", entry->entry_name,
			    (double)((double)entry->value_unsigned / 10));
		} else if (strcmp(entry->entry_name, "medium_length") == 0) {
			printf("%*s%s (%s): %ju m\n", indent, "",
			    entry->desc ?  entry->desc : "", entry->entry_name,
			    (uintmax_t)entry->value_unsigned);
		} else if (strcmp(entry->entry_name, "capacity") == 0) {
			printf("%*s%s (%s): %ju MB\n", indent, "", entry->desc ?
			    entry->desc : "", entry->entry_name,
			    (uintmax_t)entry->value_unsigned);
		} else {
			printf("%*s%s (%s): %s\n", indent, "", entry->desc ?
			    entry->desc : "", entry->entry_name, entry->value);
		}
	}

	return (retval);
}

int
mt_print_density_report(struct mt_status_entry *report_root, int indent)
{
	struct mt_status_entry *mt_report, *media_report;
	struct mt_status_entry *entry;
	int retval = 0;

	mt_report = mt_entry_find(report_root,
	    __DECONST(char *, MT_MEDIUM_TYPE_REPORT_NAME));
	if (mt_report == NULL)
		return (1);

	media_report = mt_entry_find(report_root,
	    __DECONST(char *, MT_MEDIA_REPORT_NAME));
	if (media_report == NULL)
		return (1);

	if ((mt_report->value_signed == 0)
	 && (media_report->value_signed == 0)) {
		printf("%*sThis tape drive supports the following "
		    "media densities:\n", indent, "");
	} else if ((mt_report->value_signed == 0)
		&& (media_report->value_signed != 0)) {
		printf("%*sThe tape currently in this drive supports "
		    "the following media densities:\n", indent, "");
	} else if ((mt_report->value_signed != 0)
		&& (media_report->value_signed == 0)) {
		printf("%*sThis tape drive supports the following "
		    "media types:\n", indent, "");
	} else {
		printf("%*sThis tape currently in this drive supports "
		    "the following media types:\n", indent, "");
	}

	STAILQ_FOREACH(entry, &report_root->child_entries, links) {
		struct mt_status_nv *nv;

		if (strcmp(entry->entry_name, MT_DENSITY_ENTRY_NAME) != 0)
			continue;

		STAILQ_FOREACH(nv, &entry->nv_list, links) {
			if (strcmp(nv->name, "num") != 0)
				continue;

			break;
		}

		indent += 2;

		printf("%*sDensity Entry", indent, "");
		if (nv != NULL)
			printf(" %s", nv->value);
		printf(":\n");

		retval = mt_print_density_entry(entry, indent + 2);

		indent -= 2;
	}

	return (retval);
}

int
mt_print_density(struct mt_status_entry *density_root, int indent)
{
	struct mt_status_entry *entry;
	int retval = 0;

	/*
	 * We should have this entry for every tape drive.  This particular
	 * value is reported via the mode page block header, not the
	 * SCSI REPORT DENSITY SUPPORT command.
	 */
	entry = mt_entry_find(density_root,
	    __DECONST(char *, MT_MEDIA_DENSITY_NAME));
	if (entry == NULL)
		errx(1, "Unable to find node %s", MT_MEDIA_DENSITY_NAME);
	
	printf("%*sCurrent density: %s\n", indent, "",
	    denstostring(entry->value_unsigned));

	/*
	 * It isn't an error if we don't have any density reports.  Tape
	 * drives that don't support the REPORT DENSITY SUPPORT command
	 * won't have any; they will only have the current density entry
	 * above.
	 */
	STAILQ_FOREACH(entry, &density_root->child_entries, links) {
		if (strcmp(entry->entry_name, MT_DENSITY_REPORT_NAME) != 0)
			continue;

		retval = mt_print_density_report(entry, indent);
	}

	return (retval);
}

int
mt_getdensity(int argc, char **argv, char *xml_str,
    struct mt_status_data *status_data)
{
	int retval = 0;
	int verbose = 0, xml_dump = 0;
	struct mt_status_entry *density_root = NULL;
	int c;

	while ((c = getopt(argc, argv, "vx")) != -1) {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'x':
			xml_dump = 1;
			break;
		}
	}

	if (xml_dump != 0) {
		printf("%s", xml_str);
		return (0);
	}

	density_root = mt_status_entry_find(status_data,
	    __DECONST(char *, MT_DENSITY_ROOT_NAME));
	if (density_root == NULL)
		errx(1, "Cannot find density root node %s",
		    MT_DENSITY_ROOT_NAME);

	retval = mt_print_density(density_root, 0);

	return (retval);
}

static void
warn_eof(void)
{
	fprintf(stderr,
		"The \"eof\" command has been disabled.\n"
		"Use \"weof\" if you really want to write end-of-file marks,\n"
		"or \"eom\" if you rather want to skip to the end of "
		"recorded medium.\n");
	exit(1);
}
