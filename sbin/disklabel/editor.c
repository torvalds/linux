/*	$OpenBSD: editor.c,v 1.422 2025/09/19 12:51:02 krw Exp $	*/

/*
 * Copyright (c) 1997-2000 Todd C. Miller <millert@openbsd.org>
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

#include <sys/param.h>	/* MAXBSIZE DEV_BSIZE */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>
#include <sys/sysctl.h>
#define	DKTYPENAMES
#include <sys/disklabel.h>

#include <ufs/ffs/fs.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "extern.h"
#include "pathnames.h"

#define	ROUNDUP(_s, _a)		((((_s) + (_a) - 1) / (_a)) * (_a))
#define	ROUNDDOWN(_s, _a)	(((_s) / (_a)) * (_a))
#define	CHUNKSZ(_c)		((_c)->stop - (_c)->start)

/* flags for getuint64() */
#define	DO_CONVERSIONS	0x00000001
#define	DO_ROUNDING	0x00000002

/* flags for alignpartition() */
#define	ROUND_OFFSET_UP		0x00000001
#define	ROUND_OFFSET_DOWN	0x00000002
#define	ROUND_SIZE_UP		0x00000004
#define	ROUND_SIZE_DOWN		0x00000008
#define	ROUND_SIZE_OVERLAP	0x00000010

/* Special return values for getnumber and getuint64() */
#define	CMD_ABORTED	(ULLONG_MAX - 1)
#define	CMD_BADVALUE	(ULLONG_MAX)

/* structure to describe a portion of a disk */
struct diskchunk {
	u_int64_t start;
	u_int64_t stop;
};

/* used when sorting mountpoints in mpsave() */
struct mountinfo {
	char *mountpoint;
	int partno;
};

/* used when allocating all space according to recommendations */

struct space_allocation {
	u_int64_t	minsz;	/* starts as blocks, xlated to sectors. */
	u_int64_t	maxsz;	/* starts as blocks, xlated to sectors. */
	int		rate;	/* % of extra space to use */
	char	       *mp;
};

/*
 * NOTE! Changing partition sizes in the space_allocation tables
 *       requires corresponding updates to the *.ok files in
 *	 /usr/src/regress/sbin/disklabel.
 */

/* entries for swap and var are changed by editor_allocspace() */
struct space_allocation alloc_big[] = {
	{  MEG(150),         GIG(1),   5, "/"		},
	{   MEG(80),       MEG(256),  10, "swap"	},
	{  MEG(120),         GIG(4),   8, "/tmp"	},
	{   MEG(80),         GIG(4),  13, "/var"	},
	{ MEG(1500),        GIG(30),  10, "/usr"	},
	{  MEG(384),         GIG(1),   3, "/usr/X11R6"	},
	{    GIG(1),        GIG(20),  15, "/usr/local"	},
	{    GIG(2),         GIG(5),   2, "/usr/src"	},
	{    GIG(5),         GIG(6),   4, "/usr/obj"	},
	{    GIG(1),       GIG(300),  30, "/home"	}
	/* Anything beyond this leave for the user to decide */
};

struct space_allocation alloc_medium[] = {
	{  MEG(800),         GIG(2),   5, "/"		},
	{   MEG(80),       MEG(256),  10, "swap"	},
	{ MEG(1300),         GIG(3),  78, "/usr"	},
	{  MEG(256),         GIG(2),   7, "/home"	}
};

struct space_allocation alloc_small[] = {
	{  MEG(700),         GIG(4),  95, "/"		},
	{    MEG(1),       MEG(256),   5, "swap"	}
};

struct space_allocation alloc_stupid[] = {
	{    MEG(1),      MEG(2048), 100, "/"		}
};

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

struct alloc_table {
	struct space_allocation *table;
	int sz;
};

struct alloc_table alloc_table_default[] = {
	{ alloc_big,	nitems(alloc_big) },
	{ alloc_medium,	nitems(alloc_medium) },
	{ alloc_small,	nitems(alloc_small) },
	{ alloc_stupid,	nitems(alloc_stupid) }
};
struct alloc_table *alloc_table = alloc_table_default;
int alloc_table_nitems = 4;

void	edit_packname(struct disklabel *);
void	editor_resize(struct disklabel *, const char *);
void	editor_add(struct disklabel *, const char *);
void	editor_change(struct disklabel *, const char *);
u_int64_t editor_countfree(const struct disklabel *);
void	editor_delete(struct disklabel *, const char *);
void	editor_help(void);
void	editor_modify(struct disklabel *, const char *);
void	editor_name(const struct disklabel *, const char *);
char	*getstring(const char *, const char *, const char *);
u_int64_t getuint64(const struct disklabel *, char *, char *, u_int64_t,
    u_int64_t, int *);
u_int64_t getnumber(const char *, const char *, u_int32_t, u_int32_t);
int	getpartno(const struct disklabel *, const char *, const char *);
int	has_overlap(struct disklabel *);
int	partition_cmp(const void *, const void *);
const struct partition **sort_partitions(const struct disklabel *, int);
void	find_bounds(const struct disklabel *);
void	set_bounds(struct disklabel *);
void	set_duid(struct disklabel *);
int	set_fragblock(struct disklabel *, int);
const struct diskchunk *free_chunks(const struct disklabel *, int);
int	micmp(const void *, const void *);
int	mpequal(char **, char **);
int	get_fstype(struct disklabel *, int);
int	get_mp(const struct disklabel *, int);
int	get_offset(struct disklabel *, int);
int	get_size(struct disklabel *, int);
void	zero_partitions(struct disklabel *);
u_int64_t max_partition_size(const struct disklabel *, int);
void	display_edit(const struct disklabel *, char);
void	psize(u_int64_t sz, char unit, const struct disklabel *lp);
char	*get_token(char **);
int	apply_unit(double, u_char, u_int64_t *);
int	parse_sizespec(const char *, double *, char **);
int	parse_sizerange(char *, u_int64_t *, u_int64_t *);
int	parse_pct(char *, int *);
int	alignpartition(struct disklabel *, int, u_int64_t, u_int64_t, int);
int	allocate_space(struct disklabel *, const struct alloc_table *);
void	allocate_physmemincr(struct space_allocation *);
int	allocate_partition(struct disklabel *, struct space_allocation *);
const struct diskchunk *allocate_diskchunk(const struct disklabel *,
    const struct space_allocation *);

static u_int64_t starting_sector;
static u_int64_t ending_sector;
static int resizeok;

/*
 * Simple partition editor.
 */
int
editor(int f)
{
	struct disklabel origlabel, lastlabel, tmplabel, newlab = lab;
	struct partition *pp;
	FILE *fp;
	char buf[BUFSIZ], *cmd, *arg;
	char **omountpoints = NULL;
	char **origmountpoints = NULL, **tmpmountpoints = NULL;
	int i, error = 0;

	/* Alloc and init mount point info */
	if (!(omountpoints = calloc(MAXPARTITIONS, sizeof(char *))) ||
	    !(origmountpoints = calloc(MAXPARTITIONS, sizeof(char *))) ||
	    !(tmpmountpoints = calloc(MAXPARTITIONS, sizeof(char *))))
		err(1, NULL);

	/* How big is the OpenBSD portion of the disk?  */
	find_bounds(&newlab);

	/* Make sure there is no partition overlap. */
	if (has_overlap(&newlab))
		errx(1, "can't run when there is partition overlap.");

	/* If we don't have a 'c' partition, create one. */
	pp = &newlab.d_partitions[RAW_PART];
	if (newlab.d_npartitions <= RAW_PART || DL_GETPSIZE(pp) == 0) {
		puts("No 'c' partition found, adding one that spans the disk.");
		if (newlab.d_npartitions <= RAW_PART)
			newlab.d_npartitions = RAW_PART + 1;
		DL_SETPOFFSET(pp, 0);
		DL_SETPSIZE(pp, DL_GETDSIZE(&newlab));
		pp->p_fstype = FS_UNUSED;
		pp->p_fragblock = pp->p_cpg = 0;
	}

#ifdef SUN_CYLCHECK
	if ((newlab.d_flags & D_VENDOR) && !quiet) {
		puts("This platform requires that partition offsets/sizes "
		    "be on cylinder boundaries.\n"
		    "Partition offsets/sizes will be rounded to the "
		    "nearest cylinder automatically.");
	}
#endif

	/* Save the (U|u)ndo labels and mountpoints. */
	mpcopy(origmountpoints, mountpoints);
	origlabel = newlab;
	lastlabel = newlab;

	puts("Label editor (enter '?' for help at any prompt)");
	for (;;) {
		fprintf(stdout, "%s%s> ", dkname,
		    (memcmp(&lab, &newlab, sizeof(newlab)) == 0) ? "" : "*");
		if (fgets(buf, sizeof(buf), stdin) == NULL) {
			putchar('\n');
			buf[0] = 'q';
			buf[1] = '\0';
		}
		if ((cmd = strtok(buf, " \t\r\n")) == NULL)
			continue;
		arg = strtok(NULL, " \t\r\n");

		if ((*cmd != 'u') && (*cmd != 'U')) {
			/*
			 * Save undo info in case the command tries to make
			 * changes but decides not to.
			 */
			tmplabel = lastlabel;
			lastlabel = newlab;
			mpcopy(tmpmountpoints, omountpoints);
			mpcopy(omountpoints, mountpoints);
		}

		switch (*cmd) {
		case '?':
		case 'h':
			editor_help();
			break;

		case 'A':
			if (ioctl(f, DIOCGPDINFO, &newlab) == -1) {
				warn("DIOCGPDINFO");
				newlab = lastlabel;
			} else {
				int oquiet = quiet;
				aflag = 1;
				quiet = 0;
				editor_allocspace(&newlab);
				quiet = oquiet;
			}
			break;
		case 'a':
			editor_add(&newlab, arg);
			break;

		case 'b':
			set_bounds(&newlab);
			break;

		case 'c':
			editor_change(&newlab, arg);
			break;

		case 'D':
			if (ioctl(f, DIOCGPDINFO, &newlab) == -1)
				warn("DIOCGPDINFO");
			else {
				dflag = 1;
				for (i = 0; i < MAXPARTITIONS; i++) {
					free(mountpoints[i]);
					mountpoints[i] = NULL;
				}
			}
			break;

		case 'd':
			editor_delete(&newlab, arg);
			break;

		case 'e':
			edit_packname(&newlab);
			break;

		case 'i':
			set_duid(&newlab);
			break;

		case 'm':
			editor_modify(&newlab, arg);
			break;

		case 'n':
			if (!fstabfile) {
				fputs("This option is not valid when run "
				    "without the -F or -f flags.\n", stderr);
				break;
			}
			editor_name(&newlab, arg);
			break;

		case 'p':
			display_edit(&newlab, arg ? *arg : 0);
			break;

		case 'l':
			display(stdout, &newlab, arg ? *arg : 0, 0);
			break;

		case 'M': {
			sig_t opipe = signal(SIGPIPE, SIG_IGN);
			char *pager, *comm = NULL;
			extern const u_char manpage[];
			extern const int manpage_sz;

			if ((pager = getenv("PAGER")) == NULL || *pager == '\0')
				pager = _PATH_LESS;

			if (asprintf(&comm, "gunzip -qc|%s", pager) != -1 &&
			    (fp = popen(comm, "w")) != NULL) {
				(void) fwrite(manpage, manpage_sz, 1, fp);
				pclose(fp);
			} else
				warn("unable to execute %s", pager);

			free(comm);
			(void)signal(SIGPIPE, opipe);
			break;
		}

		case 'q':
			if (donothing) {
				puts("In no change mode, not writing label.");
				goto done;
			}

			/*
			 * If we haven't changed the original label, and it
			 * wasn't a default label or an auto-allocated label,
			 * there is no need to do anything before exiting. Note
			 * that 'w' will reset dflag and aflag to allow 'q' to
			 * exit without further questions.
			 */
			if (!dflag && !aflag &&
			    memcmp(&lab, &newlab, sizeof(newlab)) == 0) {
				puts("No label changes.");
				/* Save mountpoint info. */
				mpsave(&newlab);
				goto done;
			}
			do {
				arg = getstring("Write new label?",
				    "Write the modified label to disk?",
				    "y");
			} while (arg && tolower((unsigned char)*arg) != 'y' &&
			    tolower((unsigned char)*arg) != 'n');
			if (arg && tolower((unsigned char)*arg) == 'y') {
				if (writelabel(f, &newlab) == 0) {
					newlab = lab; /* lab now has UID info */
					goto done;
				}
				warnx("unable to write label");
			}
			error = 1;
			goto done;
			/* NOTREACHED */
			break;

		case 'R':
			if (aflag && resizeok)
				editor_resize(&newlab, arg);
			else
				fputs("Resize only implemented for auto "
				    "allocated labels\n", stderr);
			has_overlap(&newlab);
			break;

		case 'r': {
			const struct diskchunk *chunk;
			uint64_t total = 0;
			/* Display free space. */
			chunk = free_chunks(&newlab, -1);
			for (; chunk->start != 0 || chunk->stop != 0; chunk++) {
				total += CHUNKSZ(chunk);
				fprintf(stderr, "Free sectors: %16llu - %16llu "
				    "(%16llu)\n",
				    chunk->start, chunk->stop - 1,
				    CHUNKSZ(chunk));
			}
			fprintf(stderr, "Total free sectors: %llu.\n", total);
			break;
		}

		case 's':
			if (arg == NULL) {
				arg = getstring("Filename",
				    "Name of the file to save label into.",
				    NULL);
				if (arg == NULL || *arg == '\0')
					break;
			}
			if ((fp = fopen(arg, "w")) == NULL) {
				warn("cannot open %s", arg);
			} else {
				display(fp, &newlab, 0, 1);
				(void)fclose(fp);
			}
			break;

		case 'U':
			/*
			 * If we allow 'U' repeatedly, information would be
			 * lost. This way multiple 'U's followed by 'u' will
			 * undo the 'U's.
			 */
			if (memcmp(&newlab, &origlabel, sizeof(newlab)) ||
			    !mpequal(mountpoints, origmountpoints)) {
				tmplabel = newlab;
				newlab = origlabel;
				lastlabel = tmplabel;
				mpcopy(tmpmountpoints, mountpoints);
				mpcopy(mountpoints, origmountpoints);
				mpcopy(omountpoints, tmpmountpoints);
			}
			puts("Original label and mount points restored.");
			break;

		case 'u':
			tmplabel = newlab;
			newlab = lastlabel;
			lastlabel = tmplabel;
			mpcopy(tmpmountpoints, mountpoints);
			mpcopy(mountpoints, omountpoints);
			mpcopy(omountpoints, tmpmountpoints);
			puts("Last change undone.");
			break;

		case 'w':
			if (donothing)  {
				puts("In no change mode, not writing label.");
				break;
			}

			/* Write label to disk. */
			if (writelabel(f, &newlab) != 0)
				warnx("unable to write label");
			else {
				dflag = aflag = 0;
				newlab = lab; /* lab now has UID info */
			}
			break;

		case 'x':
			goto done;
			break;

		case 'z':
			zero_partitions(&newlab);
			break;

		case '\n':
			break;

		default:
			printf("Unknown option: %c ('?' for help)\n", *cmd);
			break;
		}

		/*
		 * If no changes were made to label or mountpoints, then
		 * restore undo info.
		 */
		if (memcmp(&newlab, &lastlabel, sizeof(newlab)) == 0 &&
		    (mpequal(mountpoints, omountpoints))) {
			lastlabel = tmplabel;
			mpcopy(omountpoints, tmpmountpoints);
		}
	}
done:
	mpfree(omountpoints, DISCARD);
	mpfree(origmountpoints, DISCARD);
	mpfree(tmpmountpoints, DISCARD);
	return error;
}

/*
 * Allocate all disk space according to standard recommendations for a
 * root disk.
 */
int
editor_allocspace(struct disklabel *lp_org)
{
	struct disklabel label;
	struct partition *pp;
	u_int64_t pstart, pend;
	int i;

	/* How big is the OpenBSD portion of the disk?  */
	find_bounds(lp_org);

	resizeok = 1;
	for (i = 0;  i < MAXPARTITIONS; i++) {
		if (i == RAW_PART)
			continue;
		pp = &lp_org->d_partitions[i];
		if (DL_GETPSIZE(pp) == 0 || pp->p_fstype == FS_UNUSED)
			continue;
		pstart = DL_GETPOFFSET(pp);
		pend = pstart + DL_GETPSIZE(pp);
		if (((pstart >= starting_sector && pstart < ending_sector) ||
		    (pend > starting_sector && pend <= ending_sector)))
			resizeok = 0; /* Part of OBSD area is in use! */
	}

	for (i = 0; i < alloc_table_nitems; i++) {
		memcpy(&label, lp_org, sizeof(label));
		if (allocate_space(&label, &alloc_table[i]) == 0) {
			memcpy(lp_org, &label, sizeof(struct disklabel));
			return 0;
		}
	}

	return 1;
}

const struct diskchunk *
allocate_diskchunk(const struct disklabel *lp,
    const struct space_allocation *sa)
{
	const struct diskchunk *chunk;
	static struct diskchunk largest;
	uint64_t maxstop;

	largest.start = largest.stop = 0;

	chunk = free_chunks(lp, -1);
	for (; chunk->start != 0 || chunk->stop != 0; chunk++) {
		if (CHUNKSZ(chunk) > CHUNKSZ(&largest))
			largest = *chunk;
	}
	maxstop = largest.start + DL_BLKTOSEC(lp, sa->maxsz);
	if (maxstop > largest.stop)
		maxstop = largest.stop;
#ifdef SUN_CYLCHECK
	if (lp->d_flags & D_VENDOR) {
		largest.start = ROUNDUP(largest.start, lp->d_secpercyl);
		maxstop = ROUNDUP(maxstop, lp->d_secpercyl);
		if (maxstop > largest.stop)
			maxstop -= lp->d_secpercyl;
		if (largest.start >= maxstop)
			largest.start = largest.stop = maxstop = 0;
	}
#endif
	if (maxstop < largest.stop)
		largest.stop = maxstop;
	if (CHUNKSZ(&largest) < DL_BLKTOSEC(lp, sa->minsz))
		return NULL;

	return &largest;
}

int
allocate_partition(struct disklabel *lp, struct space_allocation *sa)
{
	const struct diskchunk *chunk;
	struct partition *pp;
	unsigned int partno;

	for (partno = 0; partno < MAXPARTITIONS; partno++) {
		if (partno == RAW_PART)
			continue;
		pp = &lp->d_partitions[partno];
		if (DL_GETPSIZE(pp) == 0 || pp->p_fstype == FS_UNUSED)
			break;
	}
	if (partno >= MAXPARTITIONS)
		return 1;		/* No free partition. */

	/* Find appropriate chunk of free space. */
	chunk = allocate_diskchunk(lp, sa);
	if (chunk == NULL)
		return 1;

	if (strcasecmp(sa->mp, "raid") == 0)
		pp->p_fstype = FS_RAID;
	else if (strcasecmp(sa->mp, "swap") == 0)
		pp->p_fstype = FS_SWAP;
	else if (sa->mp[0] == '/')
		pp->p_fstype = FS_BSDFFS;
	else
		return 1;

	DL_SETPSIZE(pp, chunk->stop - chunk->start);
	DL_SETPOFFSET(pp, chunk->start);

	if (pp->p_fstype == FS_BSDFFS && DL_GETPSIZE(pp) > 0) {
		mountpoints[partno] = strdup(sa->mp);
		if (mountpoints[partno] == NULL)
			err(1, NULL);
		if (set_fragblock(lp, partno))
			return 1;
	}

	return 0;
}

void
allocate_physmemincr(struct space_allocation *sa)
{
	u_int64_t memblks;
	extern int64_t physmem;

	if (physmem == 0)
		return;

	memblks = physmem / DEV_BSIZE;
	if (strcasecmp(sa->mp, "swap") == 0) {
		if (memblks < MEG(256))
			sa->minsz = sa->maxsz = 2 * memblks;
		else
			sa->maxsz += memblks;
	} else if (strcasecmp(sa->mp, "/var") == 0) {
		sa->maxsz += 2 * memblks;
	}
}

int
allocate_space(struct disklabel *lp, const struct alloc_table *alloc_table)
{
	struct space_allocation sa[MAXPARTITIONS];
	u_int64_t maxsz, xtrablks;
	int i;

	xtrablks = DL_SECTOBLK(lp, editor_countfree(lp));
	memset(sa, 0, sizeof(sa));
	for (i = 0; i < alloc_table->sz; i++) {
		sa[i] = alloc_table->table[i];
		if (alloc_table->table == alloc_big)
			allocate_physmemincr(&sa[i]);
		if (xtrablks < sa[i].minsz)
			return 1;	/* Too few free blocks. */
		xtrablks -= sa[i].minsz;
	}
	sa[alloc_table->sz - 1].rate = 100; /* Last allocation is greedy. */

	for (i = lp->d_npartitions; i < MAXPARTITIONS; i++) {
		if (i == RAW_PART)
			continue;
		memset(&lp->d_partitions[i], 0, sizeof(lp->d_partitions[i]));
	}
	lp->d_npartitions = MAXPARTITIONS;

	mpfree(mountpoints, KEEP);
	for (i = 0; i < alloc_table->sz; i++) {
		if (sa[i].rate == 100)
			maxsz = sa[i].minsz + xtrablks;
		else
			maxsz = sa[i].minsz + (xtrablks / 100) * sa[i].rate;
		if (maxsz < sa[i].maxsz)
			sa[i].maxsz = maxsz;
		if (allocate_partition(lp, &sa[i])) {
			mpfree(mountpoints, KEEP);
			return 1;
		}
	}

	return 0;
}

/*
 * Resize a partition, moving all subsequent partitions
 */
void
editor_resize(struct disklabel *lp, const char *p)
{
	struct disklabel label;
	struct partition *pp, *prev;
	u_int64_t ui, sz, off;
	int partno, i, flags, shrunk;

	label = *lp;

	if ((partno = getpartno(&label, p, "resize")) == -1)
		return;

	pp = &label.d_partitions[partno];
	sz = DL_GETPSIZE(pp);
	if (pp->p_fstype != FS_BSDFFS && pp->p_fstype != FS_SWAP) {
		fputs("Cannot resize spoofed partition\n", stderr);
		return;
	}
	flags = DO_CONVERSIONS;
	ui = getuint64(lp, "[+|-]new size (with unit)",
	    "new size or amount to grow (+) or shrink (-) partition including "
	    "unit", sz, sz + editor_countfree(lp), &flags);

	if (ui == CMD_ABORTED)
		return;
	else if (ui == CMD_BADVALUE)
		return;
	else if (ui == 0) {
		fputs("The size must be > 0 sectors\n", stderr);
		return;
	}

#ifdef SUN_CYLCHECK
	if (lp->d_flags & D_VENDOR)
		ui = ROUNDUP(ui, lp->d_secpercyl);
#endif
	if (DL_GETPOFFSET(pp) + ui > ending_sector) {
		fputs("Amount too big\n", stderr);
		return;
	}

	DL_SETPSIZE(pp, ui);
	pp->p_fragblock = 0;
	if (set_fragblock(&label, partno) == 1)
		return;

	/*
	 * Pack partitions above the resized partition, leaving unused
	 * partitions alone.
	 */
	shrunk = -1;
	prev = pp;
	for (i = partno + 1; i < MAXPARTITIONS; i++) {
		if (i == RAW_PART)
			continue;
		pp = &label.d_partitions[i];
		if (pp->p_fstype != FS_BSDFFS && pp->p_fstype != FS_SWAP)
			continue;
		sz = DL_GETPSIZE(pp);
		if (sz == 0)
			continue;

		off = DL_GETPOFFSET(prev) + DL_GETPSIZE(prev);

		if (off < ending_sector) {
			DL_SETPOFFSET(pp, off);
			if (off + DL_GETPSIZE(pp) > ending_sector) {
				DL_SETPSIZE(pp, ending_sector - off);
				pp->p_fragblock = 0;
				if (set_fragblock(&label, i) == 1)
					return;
				shrunk = i;
			}
		} else {
			fputs("Amount too big\n", stderr);
			return;
		}
		prev = pp;
	}

	if (shrunk != -1)
		fprintf(stderr, "Partition %c shrunk to %llu sectors to make "
		    "room\n", DL_PARTNUM2NAME(shrunk),
		    DL_GETPSIZE(&label.d_partitions[shrunk]));
	*lp = label;
}

/*
 * Add a new partition.
 */
void
editor_add(struct disklabel *lp, const char *p)
{
	struct partition *pp;
	const struct diskchunk *chunk;
	int partno;
	u_int64_t new_offset, new_size;

	chunk = free_chunks(lp, -1);
	new_size = new_offset = 0;
	for (; chunk->start != 0 || chunk->stop != 0; chunk++) {
		if (CHUNKSZ(chunk) > new_size) {
			new_size = CHUNKSZ(chunk);
			new_offset = chunk->start;
		}
	}

#ifdef SUN_CYLCHECK
	if ((lp->d_flags & D_VENDOR) && new_size < lp->d_secpercyl) {
		fputs("No space left, you need to shrink a partition "
		    "(need at least one full cylinder)\n",
		    stderr);
		return;
	}
#endif
	if (new_size == 0) {
		fputs("No space left, you need to shrink a partition\n",
		    stderr);
		return;
	}

	if ((partno = getpartno(lp, p, "add")) == -1)
		return;
	pp = &lp->d_partitions[partno];
	memset(pp, 0, sizeof(*pp));

	/*
	 * Increase d_npartitions if necessary. Ensure all new partitions are
	 * zero'ed to avoid inadvertent overlaps.
	 */
	for(; lp->d_npartitions <= partno; lp->d_npartitions++)
		memset(&lp->d_partitions[lp->d_npartitions], 0, sizeof(*pp));

	DL_SETPSIZE(pp, new_size);
	DL_SETPOFFSET(pp, new_offset);
	pp->p_fstype = partno == 1 ? FS_SWAP : FS_BSDFFS;

	if (get_offset(lp, partno) == 0 &&
	    get_size(lp, partno) == 0 &&
	    get_fstype(lp, partno) == 0 &&
	    get_mp(lp, partno) == 0 &&
	    set_fragblock(lp, partno) == 0)
		return;

	/* Bailed out at some point, so effectively delete the partition. */
	memset(pp, 0, sizeof(*pp));
}

/*
 * Set the mountpoint of an existing partition ('name').
 */
void
editor_name(const struct disklabel *lp, const char *p)
{
	int partno;

	if ((partno = getpartno(lp, p, "name")) == -1)
		return;

	get_mp(lp, partno);
}

/*
 * Change an existing partition.
 */
void
editor_modify(struct disklabel *lp, const char *p)
{
	struct partition opp, *pp;
	int partno;

	if ((partno = getpartno(lp, p, "modify")) == -1)
		return;

	pp = &lp->d_partitions[partno];
	opp = *pp;

	if (get_offset(lp, partno) == 0 &&
	    get_size(lp, partno) == 0   &&
	    get_fstype(lp, partno) == 0 &&
	    get_mp(lp, partno) == 0 &&
	    set_fragblock(lp, partno) == 0)
		return;

	/* Bailed out at some point, so undo any changes. */
	*pp = opp;
}

/*
 * Delete an existing partition.
 */
void
editor_delete(struct disklabel *lp, const char *p)
{
	struct partition *pp;
	int partno;

	if ((partno = getpartno(lp, p, "delete")) == -1)
		return;
	if (partno == lp->d_npartitions) {
		zero_partitions(lp);
		return;
	}
	pp = &lp->d_partitions[partno];

	/* Really delete it (as opposed to just setting to "unused") */
	memset(pp, 0, sizeof(*pp));
	free(mountpoints[partno]);
	mountpoints[partno] = NULL;
}

/*
 * Change the size of an existing partition.
 */
void
editor_change(struct disklabel *lp, const char *p)
{
	struct partition *pp;
	int partno;

	if ((partno = getpartno(lp, p, "change size")) == -1)
		return;

	pp = &lp->d_partitions[partno];
	printf("Partition %c is currently %llu sectors in size, and can have "
	    "a maximum\nsize of %llu sectors.\n",
	    DL_PARTNUM2NAME(partno), DL_GETPSIZE(pp), max_partition_size(lp, partno));

	/* Get new size */
	get_size(lp, partno);
}

/*
 * Sort the partitions based on starting offset.
 * This assumes there can be no overlap.
 */
int
partition_cmp(const void *e1, const void *e2)
{
	struct partition *p1 = *(struct partition **)e1;
	struct partition *p2 = *(struct partition **)e2;
	u_int64_t o1 = DL_GETPOFFSET(p1);
	u_int64_t o2 = DL_GETPOFFSET(p2);

	if (o1 < o2)
		return -1;
	else if (o1 > o2)
		return 1;
	else
		return 0;
}

char *
getstring(const char *prompt, const char *helpstring, const char *oval)
{
	static char buf[BUFSIZ];
	int n;

	buf[0] = '\0';
	do {
		printf("%s: [%s] ", prompt, oval ? oval : "");
		if (fgets(buf, sizeof(buf), stdin) == NULL) {
			buf[0] = '\0';
			if (feof(stdin)) {
				clearerr(stdin);
				putchar('\n');
				fputs("Command aborted\n", stderr);
				return NULL;
			}
		}
		n = strlen(buf);
		if (n > 0 && buf[n-1] == '\n')
			buf[--n] = '\0';
		if (buf[0] == '?')
			puts(helpstring);
		else if (oval != NULL && buf[0] == '\0')
			strlcpy(buf, oval, sizeof(buf));
	} while (buf[0] == '?');

	return &buf[0];
}

int
getpartno(const struct disklabel *lp, const char *p, const char *action)
{
	char buf[2] = { '\0', '\0'};
	const char *promptfmt = "partition to %s";
	const char *helpfmt = "Partition must be between 'a' and '%c' "
	    "(excluding 'c')%s.\n";
	const struct partition *pp;
	char *help = NULL, *prompt = NULL;
	unsigned char maxpart;
	int add, delete, inuse, partno;

	add = strcmp("add", action) == 0;
	delete = strcmp("delete", action) == 0;
	maxpart = 'a' - 1 + (add ? MAXPARTITIONS : lp->d_npartitions);

	if (p == NULL) {
		if (asprintf(&prompt, promptfmt, action) == -1 ||
		    asprintf(&help, helpfmt, maxpart, delete ? ", or '*'" : "")
		    == -1) {
			fprintf(stderr, "Unable to build prompt or help\n");
			goto done;
		}
		if (add) {
			/* Default to first unused partition. */
			for (partno = 0; partno < MAXPARTITIONS; partno++) {
				if (partno == RAW_PART)
					continue;
				pp = &lp->d_partitions[partno];
				if (partno >= lp->d_npartitions ||
				    DL_GETPSIZE(pp) == 0 ||
				    pp->p_fstype == FS_UNUSED) {
					buf[0] = DL_PARTNUM2NAME(partno);
					p = buf;
					break;
				}
			}
		}
		p = getstring(prompt, help, p);
		free(prompt);
		free(help);
		if (p == NULL || *p == '\0')
			goto done;
	}

	if (delete && strlen(p) == 1 && *p == '*')
		return lp->d_npartitions;

	partno = DL_PARTNAME2NUM(*p);
	if (strlen(p) > 1 || partno == -1 || *p == 'c') {
		fprintf(stderr, helpfmt, delete ? ", or '*'" : "");
		goto done;
	}

	pp = &lp->d_partitions[partno];
	inuse = partno < lp->d_npartitions && DL_GETPSIZE(pp) > 0 &&
	    pp->p_fstype != FS_UNUSED;

	if ((add && !inuse) || (!add && inuse))
		return partno;

	fprintf(stderr, "Partition '%c' is %sin use.\n", *p,
	    inuse ? "" : "not ");

 done:
	return -1;
}

/*
 * Returns
 * 0 .. CMD_ABORTED - 1	==> valid value
 * CMD_BADVALUE		==> invalid value
 * CMD_ABORTED		==> ^D on input
 */
u_int64_t
getnumber(const char *prompt, const char *helpstring, u_int32_t oval,
    u_int32_t maxval)
{
	char buf[BUFSIZ], *p;
	int rslt;
	long long rval;
	const char *errstr;

	rslt = snprintf(buf, sizeof(buf), "%u", oval);
	if (rslt < 0 || (unsigned int)rslt >= sizeof(buf))
		return CMD_BADVALUE;

	p = getstring(prompt, helpstring, buf);
	if (p == NULL)
		return CMD_ABORTED;
	if (strlen(p) == 0)
		return oval;

	rval = strtonum(p, 0, maxval, &errstr);
	if (errstr != NULL) {
		printf("%s must be between 0 and %u\n", prompt, maxval);
		return CMD_BADVALUE;
	}

	return rval;
}

/*
 * Returns
 * 0 .. CMD_ABORTED - 1	==> valid value
 * CMD_BADVALUE		==> invalid value
 * CMD_ABORTED		==> ^D on input
 */
u_int64_t
getuint64(const struct disklabel *lp, char *prompt, char *helpstring,
    u_int64_t oval, u_int64_t maxval, int *flags)
{
	char buf[21], *p, operator = '\0';
	char *unit = NULL;
	u_int64_t rval = oval;
	double d;
	int rslt;

	rslt = snprintf(buf, sizeof(buf), "%llu", oval);
	if (rslt < 0 || (unsigned int)rslt >= sizeof(buf))
		goto invalid;

	p = getstring(prompt, helpstring, buf);
	if (p == NULL)
		return CMD_ABORTED;
	else if (p[0] == '\0')
		rval = oval;
	else if (p[0] == '*' && p[1] == '\0')
		rval = maxval;
	else {
		if (*p == '+' || *p == '-')
			operator = *p++;
		if (parse_sizespec(p, &d, &unit) == -1)
			goto invalid;
		if (unit == NULL)
			rval = d;
		else if (flags != NULL && (*flags & DO_CONVERSIONS) == 0)
			goto invalid;
		else {
			switch (tolower((unsigned char)*unit)) {
			case 'b':
				rval = d / lp->d_secsize;
				break;
			case 'c':
				rval = d * lp->d_secpercyl;
				break;
			case '%':
				rval = DL_GETDSIZE(lp) * (d / 100.0);
				break;
			case '&':
				rval = maxval * (d / 100.0);
				break;
			default:
				if (apply_unit(d, *unit, &rval) == -1)
					goto invalid;
				rval = DL_BLKTOSEC(lp, rval);
				break;
			}
		}

		/* Range check then apply [+-] operator */
		if (operator == '+') {
			if (CMD_ABORTED - oval > rval)
				rval += oval;
			else {
				goto invalid;
			}
		} else if (operator == '-') {
			if (oval >= rval)
				rval = oval - rval;
			else {
				goto invalid;
			}
		}
	}

	if (flags != NULL) {
		if (unit != NULL)
			*flags |= DO_ROUNDING;
#ifdef SUN_CYLCHECK
		if (lp->d_flags & D_VENDOR)
			*flags |= DO_ROUNDING;
#endif
	}
	return rval;

invalid:
	fputs("Invalid entry\n", stderr);
	return CMD_BADVALUE;
}

/*
 * Check for partition overlap in lp and prompt the user to resolve the overlap
 * if any is found.  Returns 1 if unable to resolve, else 0.
 */
int
has_overlap(struct disklabel *lp)
{
	const struct partition **spp;
	int i, p1, p2;
	char *line = NULL;
	size_t linesize = 0;
	ssize_t linelen;

	for (;;) {
		spp = sort_partitions(lp, -1);
		for (i = 0; spp[i+1] != NULL; i++) {
			if (DL_GETPOFFSET(spp[i]) + DL_GETPSIZE(spp[i]) >
			    DL_GETPOFFSET(spp[i+1]))
				break;
		}
		if (spp[i+1] == NULL) {
			free(line);
			return 0;
		}

		p1 = DL_PARTNUM2NAME(spp[i] - lp->d_partitions);
		p2 = DL_PARTNUM2NAME(spp[i+1] - lp->d_partitions);
		printf("\nError, partitions %c and %c overlap:\n", p1, p2);
		printf("#    %16.16s %16.16s  fstype [fsize bsize    cpg]\n",
		    "size", "offset");
		display_partition(stdout, lp, DL_PARTNAME2NUM(p1), 0);
		display_partition(stdout, lp, DL_PARTNAME2NUM(p2), 0);

		for (;;) {
			printf("Disable which one? (%c %c) ", p1, p2);
			linelen = getline(&line, &linesize, stdin);
			if (linelen == -1)
				goto done;
			if (linelen == 2 && (line[0] == p1 || line[0] == p2))
				break;
		}
		lp->d_partitions[DL_PARTNAME2NUM(line[0])].p_fstype = FS_UNUSED;
	}

done:
	putchar('\n');
	free(line);
	return 1;
}

void
edit_packname(struct disklabel *lp)
{
	char *p;
	struct disklabel oldlabel = *lp;

	printf("Changing label description for %s:\n", specname);

	/* pack/label id */
	p = getstring("label name",
	    "15 char string that describes this label, usually the disk name.",
	    lp->d_packname);
	if (p == NULL) {
		*lp = oldlabel;		/* undo damage */
		return;
	}
	strncpy(lp->d_packname, p, sizeof(lp->d_packname));	/* checked */
}

const struct partition **
sort_partitions(const struct disklabel *lp, int ignore)
{
	const static struct partition *spp[MAXPARTITIONS+2];
	int i, npartitions;

	memset(spp, 0, sizeof(spp));

	for (npartitions = 0, i = 0; i < lp->d_npartitions; i++) {
		if (i != ignore && lp->d_partitions[i].p_fstype != FS_UNUSED &&
		    DL_GETPSIZE(&lp->d_partitions[i]) != 0)
			spp[npartitions++] = &lp->d_partitions[i];
	}

	/*
	 * Sort the partitions based on starting offset.
	 * This is safe because we guarantee no overlap.
	 */
	if (npartitions > 1)
		if (mergesort((void *)spp, npartitions, sizeof(spp[0]),
		    partition_cmp))
			err(4, "failed to sort partition table");

	return spp;
}

/*
 * Get beginning and ending sectors of the OpenBSD portion of the disk
 * from the user.
 */
void
set_bounds(struct disklabel *lp)
{
	u_int64_t ui, start_temp;

	/* Starting sector */
	for (;;) {
		ui = getuint64(lp, "Starting sector",
		    "The start of the OpenBSD portion of the disk.",
		    starting_sector, DL_GETDSIZE(lp), NULL);
		if (ui == CMD_ABORTED)
			return;
		else if (ui == CMD_BADVALUE)
			;	/* Try again. */
		else if (ui >= DL_GETDSIZE(lp))
			fprintf(stderr, "starting sector must be < %llu\n",
			    DL_GETDSIZE(lp));
		else
			break;
	}
	start_temp = ui;

	/* Size */
	for (;;) {
		ui = getuint64(lp, "Size ('*' for entire disk)",
		    "The size of the OpenBSD portion of the disk ('*' for the "
		    "entire disk).", ending_sector - starting_sector,
		    DL_GETDSIZE(lp) - start_temp, NULL);
		if (ui == CMD_ABORTED)
			return;
		else if (ui == CMD_BADVALUE)
			;	/* Try again. */
		else if (ui > DL_GETDSIZE(lp) - start_temp)
			fprintf(stderr, "size must be <= %llu\n",
			    DL_GETDSIZE(lp) - start_temp);
		else
			break;
	}
	ending_sector = start_temp + ui;
	DL_SETBEND(lp, ending_sector);
	starting_sector = start_temp;
	DL_SETBSTART(lp, starting_sector);
}

/*
 * Allow user to interactively change disklabel UID.
 */
void
set_duid(struct disklabel *lp)
{
	char *s;
	int i;

	printf("The disklabel UID is currently: "
	    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx\n",
	    lp->d_uid[0], lp->d_uid[1], lp->d_uid[2], lp->d_uid[3],
	    lp->d_uid[4], lp->d_uid[5], lp->d_uid[6], lp->d_uid[7]);

	do {
		s = getstring("duid", "The disklabel UID, given as a 16 "
		    "character hexadecimal string.", NULL);
		if (s == NULL || strlen(s) == 0) {
			fputs("Command aborted\n", stderr);
			return;
		}
		i = duid_parse(lp, s);
		if (i != 0)
			fputs("Invalid UID entered.\n", stderr);
	} while (i != 0);
}

/*
 * Return a list of the "chunks" of free space available
 */
const struct diskchunk *
free_chunks(const struct disklabel *lp, int partno)
{
	const struct partition **spp;
	static struct diskchunk chunks[MAXPARTITIONS + 2];
	u_int64_t start, stop;
	int i, numchunks;

	/* Sort the in-use partitions based on offset */
	spp = sort_partitions(lp, partno);

	/* If there are no partitions, it's all free. */
	if (spp[0] == NULL) {
		chunks[0].start = starting_sector;
		chunks[0].stop = ending_sector;
		chunks[1].start = chunks[1].stop = 0;
		return chunks;
	}

	/* Find chunks of free space */
	numchunks = 0;
	if (DL_GETPOFFSET(spp[0]) > starting_sector) {
		chunks[0].start = starting_sector;
		chunks[0].stop = DL_GETPOFFSET(spp[0]);
		numchunks++;
	}
	for (i = 0; spp[i] != NULL; i++) {
		start = DL_GETPOFFSET(spp[i]) + DL_GETPSIZE(spp[i]);
		if (start < starting_sector)
			start = starting_sector;
		else if (start > ending_sector)
			start = ending_sector;
		if (spp[i + 1] != NULL)
			stop = DL_GETPOFFSET(spp[i+1]);
		else
			stop = ending_sector;
		if (stop < starting_sector)
			stop = starting_sector;
		else if (stop > ending_sector)
			stop = ending_sector;
		if (start < stop) {
			chunks[numchunks].start = start;
			chunks[numchunks].stop = stop;
			numchunks++;
		}
	}

	/* Terminate and return */
	chunks[numchunks].start = chunks[numchunks].stop = 0;
	return chunks;
}

void
find_bounds(const struct disklabel *lp)
{
	starting_sector = DL_GETBSTART(lp);
	ending_sector = DL_GETBEND(lp);

	if (ending_sector) {
		if (verbose)
			printf("Treating sectors %llu-%llu as the OpenBSD"
			    " portion of the disk.\nYou can use the 'b'"
			    " command to change this.\n\n", starting_sector,
			    ending_sector);
	}
}

/*
 * Calculate free space.
 */
u_int64_t
editor_countfree(const struct disklabel *lp)
{
	const struct diskchunk *chunk;
	u_int64_t freesectors = 0;

	chunk = free_chunks(lp, -1);

	for (; chunk->start != 0 || chunk->stop != 0; chunk++)
		freesectors += CHUNKSZ(chunk);

	return freesectors;
}

void
editor_help(void)
{
	puts("Available commands:");
	puts(
" ? | h    - show help                 n [part] - set mount point\n"
" A        - auto partition all space  p [unit] - print partitions\n"
" a [part] - add partition             q        - quit & save changes\n"
" b        - set OpenBSD boundaries    R [part] - resize auto allocated partition\n"
" c [part] - change partition size     r        - display free space\n"
" D        - reset label to default    s [path] - save label to file\n"
" d [part] - delete partition          U        - undo all changes\n"
" e        - edit label description    u        - undo last change\n"
" i        - modify disklabel UID      w        - write label to disk\n"
" l [unit] - print disk label header   x        - exit & lose changes\n"
" M        - disklabel(8) man page     z        - delete all partitions\n"
" m [part] - modify partition\n"
"\n"
"Suffixes can be used to indicate units other than sectors:\n"
" 'b' (bytes), 'k' (kilobytes), 'm' (megabytes), 'g' (gigabytes) 't' (terabytes)\n"
" 'c' (cylinders), '%' (% of total disk), '&' (% of free space).\n"
"Values in non-sector units are truncated to the nearest cylinder boundary.");

}

void
mpcopy(char **to, char **from)
{
	int i;

	for (i = 0; i < MAXPARTITIONS; i++) {
		free(to[i]);
		to[i] = NULL;
		if (from[i] != NULL) {
			to[i] = strdup(from[i]);
			if (to[i] == NULL)
				err(1, NULL);
		}
	}
}

int
mpequal(char **mp1, char **mp2)
{
	int i;

	for (i = 0; i < MAXPARTITIONS; i++) {
		if (mp1[i] == NULL && mp2[i] == NULL)
			continue;

		if ((mp1[i] != NULL && mp2[i] == NULL) ||
		    (mp1[i] == NULL && mp2[i] != NULL) ||
		    (strcmp(mp1[i], mp2[i]) != 0))
			return 0;
	}
	return 1;
}

void
mpsave(const struct disklabel *lp)
{
	int i, j;
	char bdev[PATH_MAX], *p;
	struct mountinfo mi[MAXPARTITIONS];
	FILE *fp;
	u_int8_t fstype;

	if (!fstabfile)
		return;

	memset(&mi, 0, sizeof(mi));

	for (i = 0; i < MAXPARTITIONS; i++) {
		fstype = lp->d_partitions[i].p_fstype;
		if (mountpoints[i] != NULL || fstype == FS_SWAP) {
			mi[i].mountpoint = mountpoints[i];
			mi[i].partno = i;
		}
	}

	/* Convert specname to bdev */
	if (uidflag) {
		snprintf(bdev, sizeof(bdev),
		    "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx.%c",
		    lab.d_uid[0], lab.d_uid[1], lab.d_uid[2], lab.d_uid[3],
		    lab.d_uid[4], lab.d_uid[5], lab.d_uid[6], lab.d_uid[7],
		    specname[strlen(specname)-1]);
	} else if (strncmp(_PATH_DEV, specname, sizeof(_PATH_DEV) - 1) == 0 &&
	    specname[sizeof(_PATH_DEV) - 1] == 'r') {
		snprintf(bdev, sizeof(bdev), "%s%s", _PATH_DEV,
		    &specname[sizeof(_PATH_DEV)]);
	} else {
		if ((p = strrchr(specname, '/')) == NULL || *(++p) != 'r')
			return;
		*p = '\0';
		snprintf(bdev, sizeof(bdev), "%s%s", specname, p + 1);
		*p = 'r';
	}
	bdev[strlen(bdev) - 1] = '\0';

	/* Sort mountpoints so we don't try to mount /usr/local before /usr */
	qsort((void *)mi, MAXPARTITIONS, sizeof(struct mountinfo), micmp);

	if ((fp = fopen(fstabfile, "w"))) {
		for (i = 0; i < MAXPARTITIONS; i++) {
			j =  mi[i].partno;
			fstype = lp->d_partitions[j].p_fstype;
			if (fstype == FS_RAID)
				continue;
			if (fstype == FS_SWAP) {
				fprintf(fp, "%s%c none swap sw\n", bdev,
				    DL_PARTNUM2NAME(j));
			} else if (mi[i].mountpoint) {
				fprintf(fp, "%s%c %s %s rw 1 %d\n", bdev,
				    DL_PARTNUM2NAME(j), mi[i].mountpoint,
				    fstypesnames[fstype], j == 0 ? 1 : 2);
			}
		}
		fclose(fp);
	}
}

void
mpfree(char **mp, int action)
{
	int part;

	if (mp == NULL)
		return;

	for (part = 0; part < MAXPARTITIONS; part++) {
		free(mp[part]);
		mp[part] = NULL;
	}

	if (action == DISCARD) {
		free(mp);
		mp = NULL;
	}
}

int
get_offset(struct disklabel *lp, int partno)
{
	struct partition opp, *pp = &lp->d_partitions[partno];
	u_int64_t ui, offsetalign;
	int flags;

	flags = DO_CONVERSIONS;
	ui = getuint64(lp, "offset",
	    "Starting sector for this partition.",
	    DL_GETPOFFSET(pp),
	    DL_GETPOFFSET(pp), &flags);

	if (ui == CMD_ABORTED || ui == CMD_BADVALUE)
		return 1;
#ifdef SUN_AAT0
	if (partno == 0 && ui != 0) {
		fprintf(stderr, "This architecture requires that "
		    "partition 'a' start at sector 0.\n");
		return 1;
	}
#endif
	opp = *pp;
	DL_SETPOFFSET(pp, ui);
	offsetalign = 1;
	if ((flags & DO_ROUNDING) != 0 && pp->p_fstype == FS_BSDFFS)
		offsetalign = lp->d_secpercyl;

	if (alignpartition(lp, partno, offsetalign, 1, ROUND_OFFSET_UP) == 1) {
		*pp = opp;
		return 1;
	}

	return 0;
}

int
get_size(struct disklabel *lp, int partno)
{
	struct partition opp, *pp = &lp->d_partitions[partno];
	u_int64_t maxsize, ui, sizealign;
	int flags;

	maxsize = max_partition_size(lp, partno);
	flags = DO_CONVERSIONS;
	ui = getuint64(lp, "size", "Size of the partition. "
	    "You may also say +/- amount for a relative change.",
	    DL_GETPSIZE(pp), maxsize, &flags);

	if (ui == CMD_ABORTED || ui == CMD_BADVALUE)
		return 1;

	opp = *pp;
	DL_SETPSIZE(pp, ui);
	sizealign = 1;
	if ((flags & DO_ROUNDING) != 0 && (pp->p_fstype == FS_SWAP ||
	    pp->p_fstype == FS_BSDFFS))
		sizealign = lp->d_secpercyl;

	if (alignpartition(lp, partno, 1, sizealign, ROUND_SIZE_UP) == 1) {
		*pp = opp;
		return 1;
	}

	return 0;
}

int
set_fragblock(struct disklabel *lp, int partno)
{
	struct partition opp, *pp = &lp->d_partitions[partno];
	u_int64_t bytes, offsetalign, sizealign;
	u_int32_t frag, fsize;

	if (pp->p_fstype != FS_BSDFFS)
		return 0;

	if (pp->p_cpg == 0)
		pp->p_cpg = 1;

	fsize = DISKLABELV1_FFS_FSIZE(pp->p_fragblock);
	frag = DISKLABELV1_FFS_FRAG(pp->p_fragblock);
	if (fsize == 0) {
		fsize = 2048;
		frag = 8;
		bytes = DL_GETPSIZE(pp) * lp->d_secsize;
		if (bytes > 128ULL * 1024 * 1024 * 1024)
			fsize *= 2;
		if (bytes > 512ULL * 1024 * 1024 * 1024)
			fsize *= 2;
		if (fsize < lp->d_secsize)
			fsize = lp->d_secsize;
		if (fsize > MAXBSIZE / frag)
			fsize = MAXBSIZE / frag;
		pp->p_fragblock = DISKLABELV1_FFS_FRAGBLOCK(fsize, frag);
	}
#ifdef SUN_CYLCHECK
	return 0;
#endif
	opp = *pp;
	sizealign = (DISKLABELV1_FFS_FRAG(pp->p_fragblock) *
	    DISKLABELV1_FFS_FSIZE(pp->p_fragblock)) / lp->d_secsize;
	offsetalign = 1;
	if (DL_GETPOFFSET(pp) != starting_sector)
		offsetalign = sizealign;

	if (alignpartition(lp, partno, offsetalign, sizealign, ROUND_OFFSET_UP |
	    ROUND_SIZE_DOWN | ROUND_SIZE_OVERLAP) == 1) {
		*pp = opp;
		return 1;
	}

	return 0;
}

int
get_fstype(struct disklabel *lp, int partno)
{
	char *p;
	u_int64_t ui;
	struct partition *pp = &lp->d_partitions[partno];

	if (pp->p_fstype < FSMAXTYPES) {
		p = getstring("FS type",
		    "Filesystem type (usually 4.2BSD or swap)",
		    fstypenames[pp->p_fstype]);
		if (p == NULL) {
			return 1;
		}
		for (ui = 0; ui < FSMAXTYPES; ui++) {
			if (!strcasecmp(p, fstypenames[ui])) {
				pp->p_fstype = ui;
				break;
			}
		}
		if (ui >= FSMAXTYPES) {
			printf("Unrecognized filesystem type '%s', treating "
			    "as 'unknown'\n", p);
			pp->p_fstype = FS_OTHER;
		}
	} else {
		for (;;) {
			ui = getnumber("FS type (decimal)",
			    "Filesystem type as a decimal number; usually 7 "
			    "(4.2BSD) or 1 (swap).",
			    pp->p_fstype, UINT8_MAX);
			if (ui == CMD_ABORTED)
				return 1;
			else if (ui == CMD_BADVALUE)
				;	/* Try again. */
			else
				break;
		}
		pp->p_fstype = ui;
	}
	return 0;
}

int
get_mp(const struct disklabel *lp, int partno)
{
	const struct partition *pp = &lp->d_partitions[partno];
	char *p;
	int i;

	if (fstabfile == NULL ||
	    pp->p_fstype == FS_UNUSED ||
	    pp->p_fstype == FS_SWAP ||
	    pp->p_fstype == FS_BOOT ||
	    pp->p_fstype == FS_OTHER ||
	    pp->p_fstype == FS_RAID) {
		/* No fstabfile, no names. Not all fstypes can be named */
		return 0;
	}

	for (;;) {
		p = getstring("mount point",
		    "Where to mount this filesystem (ie: / /var /usr)",
		    mountpoints[partno] ? mountpoints[partno] : "none");
		if (p == NULL)
			return 1;
		if (strcasecmp(p, "none") == 0) {
			free(mountpoints[partno]);
			mountpoints[partno] = NULL;
			break;
		}
		for (i = 0; i < MAXPARTITIONS; i++)
			if (mountpoints[i] != NULL && i != partno &&
			    strcmp(p, mountpoints[i]) == 0)
				break;
		if (i < MAXPARTITIONS) {
			fprintf(stderr, "'%c' already being mounted at "
			    "'%s'\n", DL_PARTNUM2NAME(i), p);
			break;
		}
		if (*p == '/') {
			/* XXX - might as well realloc */
			free(mountpoints[partno]);
			if ((mountpoints[partno] = strdup(p)) == NULL)
				err(1, NULL);
			break;
		}
		fputs("Mount points must start with '/'\n", stderr);
	}

	return 0;
}

int
micmp(const void *a1, const void *a2)
{
	struct mountinfo *mi1 = (struct mountinfo *)a1;
	struct mountinfo *mi2 = (struct mountinfo *)a2;

	/* We want all the NULLs at the end... */
	if (mi1->mountpoint == NULL && mi2->mountpoint == NULL)
		return 0;
	else if (mi1->mountpoint == NULL)
		return 1;
	else if (mi2->mountpoint == NULL)
		return -1;
	else
		return strcmp(mi1->mountpoint, mi2->mountpoint);
}

void
zero_partitions(struct disklabel *lp)
{
	memset(lp->d_partitions, 0, sizeof(lp->d_partitions));
	DL_SETPSIZE(&lp->d_partitions[RAW_PART], DL_GETDSIZE(lp));

	mpfree(mountpoints, KEEP);
}

u_int64_t
max_partition_size(const struct disklabel *lp, int partno)
{
	const struct diskchunk *chunk;
	u_int64_t maxsize = 0, offset;

	chunk = free_chunks(lp, partno);

	offset = DL_GETPOFFSET(&lp->d_partitions[partno]);
	for (; chunk->start != 0 || chunk->stop != 0; chunk++) {
		if (offset < chunk->start || offset >= chunk->stop)
			continue;
		maxsize = chunk->stop - offset;
		break;
	}
	return maxsize;
}

void
psize(u_int64_t sz, char unit, const struct disklabel *lp)
{
	double d = scale(sz, unit, lp);
	if (d < 0)
		printf("%llu", sz);
	else
		printf("%.*f%c", unit == 'B' ? 0 : 1, d, unit);
}

void
display_edit(const struct disklabel *lp, char unit)
{
	u_int64_t fr;
	int i;

	fr = editor_countfree(lp);
	unit = canonical_unit(lp, unit);

	printf("OpenBSD area: ");
	psize(starting_sector, 0, lp);
	printf("-");
	psize(ending_sector, 0, lp);
	printf("; size: ");
	psize(ending_sector - starting_sector, unit, lp);
	printf("; free: ");
	psize(fr, unit, lp);

	printf("\n#    %16.16s %16.16s  fstype [fsize bsize   cpg]\n",
	    "size", "offset");
	for (i = 0; i < lp->d_npartitions; i++)
		display_partition(stdout, lp, i, unit);
}

void
parse_autotable(char *filename)
{
	FILE	*cfile;
	size_t	 linesize = 0;
	char	*line = NULL, *buf, *t;
	uint	 idx = 0, pctsum = 0;
	struct space_allocation *sa;

	if (strcmp(filename, "-") == 0)
		cfile = stdin;
	else if ((cfile = fopen(filename, "r")) == NULL)
		err(1, "%s", filename);
	if ((alloc_table = calloc(1, sizeof(struct alloc_table))) == NULL)
		err(1, NULL);
	alloc_table_nitems = 1;

	while (getline(&line, &linesize, cfile) != -1) {
		if ((alloc_table[0].table = reallocarray(alloc_table[0].table,
		    idx + 1, sizeof(*sa))) == NULL)
			err(1, NULL);
		sa = &(alloc_table[0].table[idx]);
		memset(sa, 0, sizeof(*sa));
		idx++;

		buf = line;
		if ((sa->mp = get_token(&buf)) == NULL ||
		    (sa->mp[0] != '/' && strcasecmp(sa->mp, "swap") &&
		    strcasecmp(sa->mp, "raid")))
			errx(1, "%s: parse error on line %u", filename, idx);
		if ((t = get_token(&buf)) == NULL ||
		    parse_sizerange(t, &sa->minsz, &sa->maxsz) == -1)
			errx(1, "%s: parse error on line %u", filename, idx);
		if ((t = get_token(&buf)) != NULL &&
		    parse_pct(t, &sa->rate) == -1)
			errx(1, "%s: parse error on line %u", filename, idx);
		if (sa->minsz > sa->maxsz)
			errx(1, "%s: min size > max size on line %u", filename,
			    idx);
		pctsum += sa->rate;
	}
	if (pctsum > 100)
		errx(1, "%s: sum of extra space allocation > 100%%", filename);
	alloc_table[0].sz = idx;
	free(line);
	fclose(cfile);
}

char *
get_token(char **s)
{
	char	*p, *r;
	size_t	 tlen = 0;

	p = *s;
	while (**s != '\0' && !isspace((u_char)**s)) {
		(*s)++;
		tlen++;
	}
	if (tlen == 0)
		return NULL;

	/* eat whitespace */
	while (isspace((u_char)**s))
		(*s)++;

	if ((r = strndup(p, tlen)) == NULL)
		err(1, NULL);
	return r;
}

int
apply_unit(double val, u_char unit, u_int64_t *n)
{
	u_int64_t factor = 1;

	switch (tolower(unit)) {
	case 't':
		factor *= 1024;
		/* FALLTHROUGH */
	case 'g':
		factor *= 1024;
		/* FALLTHROUGH */
	case 'm':
		factor *= 1024;
		/* FALLTHROUGH */
	case 'k':
		factor *= 1024;
		break;
	default:
		return -1;
	}

	val *= factor / DEV_BSIZE;
	if (val > (double)ULLONG_MAX)
		return -1;
	*n = val;
	return 0;
}

int
parse_sizespec(const char *buf, double *val, char **unit)
{
	errno = 0;
	*val = strtod(buf, unit);
	if (errno == ERANGE || *val < 0 || *val > (double)ULLONG_MAX)
		return -1;	/* too big/small */
	if (*val == 0 && *unit == buf)
		return -1;	/* No conversion performed. */
	if (*unit != NULL && *unit[0] == '\0')
		*unit = NULL;
	return 0;
}

int
parse_sizerange(char *buf, u_int64_t *min, u_int64_t *max)
{
	char	*p, *unit1 = NULL, *unit2 = NULL;
	double	 val1 = 0, val2 = 0;

	if (strcmp(buf, "*") == 0) {
		*min = 0;
		*max = UINT64_MAX;
		goto done;
	}

	if ((p = strchr(buf, '-')) != NULL) {
		p[0] = '\0';
		p++;
	}
	*max = 0;
	if (parse_sizespec(buf, &val1, &unit1) == -1)
		return -1;
	if (p != NULL && p[0] != '\0') {
		if (p[0] == '*')
			*max = UINT64_MAX;
		else
			if (parse_sizespec(p, &val2, &unit2) == -1)
				return -1;
	}
	if (unit1 == NULL && (unit1 = unit2) == NULL)
		return -1;
	if (apply_unit(val1, unit1[0], min) == -1)
		return -1;
	if (val2 > 0) {
		if (apply_unit(val2, unit2[0], max) == -1)
			return -1;
	} else
		if (*max == 0)
			*max = *min;
 done:
	free(buf);
	return 0;
}

int
parse_pct(char *buf, int *n)
{
	const char	*errstr;

	if (buf[strlen(buf) - 1] == '%')
		buf[strlen(buf) - 1] = '\0';
	*n = strtonum(buf, 0, 100, &errstr);
	if (errstr) {
		warnx("parse percent %s: %s", buf, errstr);
		return -1;
	}
	free(buf);
	return 0;
}

int
alignpartition(struct disklabel *lp, int partno, u_int64_t startalign,
    u_int64_t stopalign, int flags)
{
	struct partition *pp = &lp->d_partitions[partno];
	const struct diskchunk *chunk;
	u_int64_t start, stop, maxstop;

	start = DL_GETPOFFSET(pp);
	if ((flags & ROUND_OFFSET_UP) == ROUND_OFFSET_UP)
		start = ROUNDUP(start, startalign);
	else if ((flags & ROUND_OFFSET_DOWN) == ROUND_OFFSET_DOWN)
		start = ROUNDDOWN(start, startalign);

	/* Find the chunk that contains 'start'. */
	chunk = free_chunks(lp, partno);
	for (; chunk->start != 0 || chunk->stop != 0; chunk++) {
		if (start >= chunk->start && start < chunk->stop)
			break;
	}
	if (chunk->stop == 0) {
		fprintf(stderr, "'%c' aligned offset %llu lies outside "
		    "the OpenBSD bounds or inside another partition\n",
		    DL_PARTNUM2NAME(partno), start);
		return 1;
	}

	/* Calculate the new 'stop' sector, the sector after the partition. */
	if ((flags & ROUND_SIZE_OVERLAP) == 0)
		maxstop = ROUNDDOWN(chunk->stop, stopalign);
	else
		maxstop = ROUNDDOWN(ending_sector, stopalign);

	stop = DL_GETPOFFSET(pp) + DL_GETPSIZE(pp);
	if ((flags & ROUND_SIZE_UP) == ROUND_SIZE_UP)
		stop = ROUNDUP(stop, stopalign);
	else if ((flags & ROUND_SIZE_DOWN) == ROUND_SIZE_DOWN)
		stop = ROUNDDOWN(stop, stopalign);
	if (stop > maxstop)
		stop = maxstop;

	if (stop <= start) {
		fprintf(stderr, "not enough space\n");
		return 1;
	}

	if (start != DL_GETPOFFSET(pp))
		DL_SETPOFFSET(pp, start);
	if (stop != DL_GETPOFFSET(pp) + DL_GETPSIZE(pp))
		DL_SETPSIZE(pp, stop - start);

	return 0;
}
