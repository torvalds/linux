/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * Copyright (c) 2009, Fabien Thomas
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

/*
 * Transform a hwpmc(4) log into human readable form, and into
 * gprof(1) compatible profiles.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/gmon.h>
#include <sys/imgact_aout.h>
#include <sys/imgact_elf.h>
#include <sys/mman.h>
#include <sys/pmc.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <pmc.h>
#include <pmclog.h>
#include <sysexits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pmcstat.h"
#include "pmcstat_log.h"
#include "pmcpl_callgraph.h"
#include "pmcpl_gprof.h"

typedef	uint64_t	WIDEHISTCOUNTER;

#define	min(A,B)		((A) < (B) ? (A) : (B))
#define	max(A,B)		((A) > (B) ? (A) : (B))

#define	WIDEHISTCOUNTER_MAX		UINT64_MAX
#define	HISTCOUNTER_MAX			USHRT_MAX
#define	WIDEHISTCOUNTER_GMONTYPE	((int) 64)
#define	HISTCOUNTER_GMONTYPE		((int) 0)
static int hc_sz=0;

/*
 * struct pmcstat_gmonfile tracks a given 'gmon.out' file.  These
 * files are mmap()'ed in as needed.
 */

struct pmcstat_gmonfile {
	LIST_ENTRY(pmcstat_gmonfile)	pgf_next; /* list of entries */
	int		pgf_overflow;	/* whether a count overflowed */
	pmc_id_t	pgf_pmcid;	/* id of the associated pmc */
	size_t		pgf_nbuckets;	/* #buckets in this gmon.out */
	unsigned int	pgf_nsamples;	/* #samples in this gmon.out */
	pmcstat_interned_string pgf_name;	/* pathname of gmon.out file */
	size_t		pgf_ndatabytes;	/* number of bytes mapped */
	void		*pgf_gmondata;	/* pointer to mmap'ed data */
	FILE		*pgf_file;	/* used when writing gmon arcs */
};

/*
 * Prototypes
 */

static void	pmcstat_gmon_create_file(struct pmcstat_gmonfile *_pgf,
    struct pmcstat_image *_image);
static pmcstat_interned_string pmcstat_gmon_create_name(const char *_sd,
    struct pmcstat_image *_img, pmc_id_t _pmcid);
static void	pmcstat_gmon_map_file(struct pmcstat_gmonfile *_pgf);
static void	pmcstat_gmon_unmap_file(struct pmcstat_gmonfile *_pgf);

static struct pmcstat_gmonfile *pmcstat_image_find_gmonfile(struct
    pmcstat_image *_i, pmc_id_t _id);

/*
 * Create a gmon.out file and size it.
 */

static void
pmcstat_gmon_create_file(struct pmcstat_gmonfile *pgf,
    struct pmcstat_image *image)
{
	int fd;
	size_t count;
	struct gmonhdr gm;
	const char *pathname;
	char buffer[DEFAULT_BUFFER_SIZE];

	pathname = pmcstat_string_unintern(pgf->pgf_name);
	if ((fd = open(pathname, O_RDWR|O_NOFOLLOW|O_CREAT,
		 S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) < 0)
		err(EX_OSERR, "ERROR: Cannot open \"%s\"", pathname);

	gm.lpc = image->pi_start;
	gm.hpc = image->pi_end;
	gm.ncnt = (pgf->pgf_nbuckets * hc_sz) + sizeof(struct gmonhdr);
	gm.version = GMONVERSION;
	gm.profrate = 0;		/* use ticks */
	if (args.pa_flags & FLAG_DO_WIDE_GPROF_HC)
		gm.histcounter_type = WIDEHISTCOUNTER_GMONTYPE;
	else
		gm.histcounter_type = HISTCOUNTER_GMONTYPE;
	gm.spare[0] = gm.spare[1] = 0;

	/* Write out the gmon header */
	if (write(fd, &gm, sizeof(gm)) < 0)
		goto error;

	/* Zero fill the samples[] array */
	(void) memset(buffer, 0, sizeof(buffer));

	count = pgf->pgf_ndatabytes - sizeof(struct gmonhdr);
	while (count > sizeof(buffer)) {
		if (write(fd, &buffer, sizeof(buffer)) < 0)
			goto error;
		count -= sizeof(buffer);
	}

	if (write(fd, &buffer, count) < 0)
		goto error;

	(void) close(fd);

	return;

 error:
	err(EX_OSERR, "ERROR: Cannot write \"%s\"", pathname);
}

/*
 * Determine the full pathname of a gmon.out file for a given
 * (image,pmcid) combination.  Return the interned string.
 */

pmcstat_interned_string
pmcstat_gmon_create_name(const char *samplesdir, struct pmcstat_image *image,
    pmc_id_t pmcid)
{
	const char *pmcname;
	char fullpath[PATH_MAX];

	pmcname = pmcstat_pmcid_to_name(pmcid);
	if (!pmcname)
		err(EX_SOFTWARE, "ERROR: cannot find pmcid");

	(void) snprintf(fullpath, sizeof(fullpath),
	    "%s/%s/%s", samplesdir, pmcname,
	    pmcstat_string_unintern(image->pi_samplename));

	return (pmcstat_string_intern(fullpath));
}


/*
 * Mmap in a gmon.out file for processing.
 */

static void
pmcstat_gmon_map_file(struct pmcstat_gmonfile *pgf)
{
	int fd;
	const char *pathname;

	pathname = pmcstat_string_unintern(pgf->pgf_name);

	/* the gmon.out file must already exist */
	if ((fd = open(pathname, O_RDWR | O_NOFOLLOW, 0)) < 0)
		err(EX_OSERR, "ERROR: cannot open \"%s\"", pathname);

	pgf->pgf_gmondata = mmap(NULL, pgf->pgf_ndatabytes,
	    PROT_READ|PROT_WRITE, MAP_NOSYNC|MAP_SHARED, fd, 0);

	if (pgf->pgf_gmondata == MAP_FAILED)
		err(EX_OSERR, "ERROR: cannot map \"%s\"", pathname);

	(void) close(fd);
}

/*
 * Unmap a gmon.out file after sync'ing its data to disk.
 */

static void
pmcstat_gmon_unmap_file(struct pmcstat_gmonfile *pgf)
{
	(void) msync(pgf->pgf_gmondata, pgf->pgf_ndatabytes,
	    MS_SYNC);
	(void) munmap(pgf->pgf_gmondata, pgf->pgf_ndatabytes);
	pgf->pgf_gmondata = NULL;
}

static void
pmcstat_gmon_append_arc(struct pmcstat_image *image, pmc_id_t pmcid,
    uintptr_t rawfrom, uintptr_t rawto, uint32_t count)
{
	struct rawarc arc;	/* from <sys/gmon.h> */
	const char *pathname;
	struct pmcstat_gmonfile *pgf;

	if ((pgf = pmcstat_image_find_gmonfile(image, pmcid)) == NULL)
		return;

	if (pgf->pgf_file == NULL) {
		pathname = pmcstat_string_unintern(pgf->pgf_name);
		if ((pgf->pgf_file = fopen(pathname, "a")) == NULL)
			return;
	}

	arc.raw_frompc = rawfrom + image->pi_vaddr;
	arc.raw_selfpc = rawto + image->pi_vaddr;
	arc.raw_count = count;

	(void) fwrite(&arc, sizeof(arc), 1, pgf->pgf_file);

}

static struct pmcstat_gmonfile *
pmcstat_image_find_gmonfile(struct pmcstat_image *image, pmc_id_t pmcid)
{
	struct pmcstat_gmonfile *pgf;
	LIST_FOREACH(pgf, &image->pi_gmlist, pgf_next)
	    if (pgf->pgf_pmcid == pmcid)
		    return (pgf);
	return (NULL);
}

static void
pmcstat_cgnode_do_gmon_arcs(struct pmcstat_cgnode *cg, pmc_id_t pmcid)
{
	struct pmcstat_cgnode *cgc;

	/*
	 * Look for child nodes that belong to the same image.
	 */

	LIST_FOREACH(cgc, &cg->pcg_children, pcg_sibling) {
		if (cgc->pcg_image == cg->pcg_image)
			pmcstat_gmon_append_arc(cg->pcg_image, pmcid,
			    cgc->pcg_func, cg->pcg_func, cgc->pcg_count);
		if (cgc->pcg_nchildren > 0)
			pmcstat_cgnode_do_gmon_arcs(cgc, pmcid);
	}
}

static void
pmcstat_callgraph_do_gmon_arcs_for_pmcid(pmc_id_t pmcid)
{
	int n;
	struct pmcstat_cgnode_hash *pch;

	for (n = 0; n < PMCSTAT_NHASH; n++)
		LIST_FOREACH(pch, &pmcstat_cgnode_hash[n], pch_next)
			if (pch->pch_pmcid == pmcid &&
			    pch->pch_cgnode->pcg_nchildren > 1)
				pmcstat_cgnode_do_gmon_arcs(pch->pch_cgnode,
				    pmcid);
}


static void
pmcstat_callgraph_do_gmon_arcs(void)
{
	struct pmcstat_pmcrecord *pmcr;

	LIST_FOREACH(pmcr, &pmcstat_pmcs, pr_next)
		pmcstat_callgraph_do_gmon_arcs_for_pmcid(pmcr->pr_pmcid);
}

void
pmcpl_gmon_initimage(struct pmcstat_image *pi)
{
	const char *execpath;
	int count, nlen;
	char *sn, *snbuf;
	char name[NAME_MAX];

	/*
	 * Look for a suitable name for the sample files associated
	 * with this image: if `basename(path)`+".gmon" is available,
	 * we use that, otherwise we try iterating through
	 * `basename(path)`+ "~" + NNN + ".gmon" till we get a free
	 * entry.
	 */
	execpath = pmcstat_string_unintern(pi->pi_execpath);
	if ((snbuf = strdup(execpath)) == NULL)
		err(EX_OSERR, "ERROR: Cannot copy \"%s\"", execpath);
	if ((sn = basename(snbuf)) == NULL)
		err(EX_OSERR, "ERROR: Cannot process \"%s\"", execpath);

	nlen = strlen(sn);
	nlen = min(nlen, (int) (sizeof(name) - sizeof(".gmon")));

	snprintf(name, sizeof(name), "%.*s.gmon", nlen, sn);

	/* try use the unabridged name first */
	if (pmcstat_string_lookup(name) == NULL)
		pi->pi_samplename = pmcstat_string_intern(name);
	else {
		/*
		 * Otherwise use a prefix from the original name and
		 * up to 3 digits.
		 */
		nlen = strlen(sn);
		nlen = min(nlen, (int) (sizeof(name)-sizeof("~NNN.gmon")));
		count = 0;
		do {
			if (++count > 999)
				errx(EX_CANTCREAT,
				    "ERROR: cannot create a gmon file for"
				    " \"%s\"", name);
			snprintf(name, sizeof(name), "%.*s~%3.3d.gmon",
			    nlen, sn, count);
			if (pmcstat_string_lookup(name) == NULL) {
				pi->pi_samplename =
				    pmcstat_string_intern(name);
				count = 0;
			}
		} while (count > 0);
	}
	free(snbuf);

	LIST_INIT(&pi->pi_gmlist);
}

void
pmcpl_gmon_shutdownimage(struct pmcstat_image *pi)
{
	struct pmcstat_gmonfile *pgf, *pgftmp;

	LIST_FOREACH_SAFE(pgf, &pi->pi_gmlist, pgf_next, pgftmp) {
		if (pgf->pgf_file)
			(void) fclose(pgf->pgf_file);
		LIST_REMOVE(pgf, pgf_next);
		free(pgf);
	}
}

void
pmcpl_gmon_newpmc(pmcstat_interned_string ps, struct pmcstat_pmcrecord *pr)
{
	struct stat st;
	char fullpath[PATH_MAX];

	(void) pr;

	/*
	 * Create the appropriate directory to hold gmon.out files.
	 */

	(void) snprintf(fullpath, sizeof(fullpath), "%s/%s", args.pa_samplesdir,
	    pmcstat_string_unintern(ps));

	/* If the path name exists, it should be a directory */
	if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
		return;

	if (mkdir(fullpath, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) < 0)
		err(EX_OSERR, "ERROR: Cannot create directory \"%s\"",
		    fullpath);
}

/*
 * Increment the bucket in the gmon.out file corresponding to 'pmcid'
 * and 'pc'.
 */

void
pmcpl_gmon_process(struct pmcstat_process *pp, struct pmcstat_pmcrecord *pmcr,
    uint32_t nsamples, uintfptr_t *cc, int usermode, uint32_t cpu)
{
	struct pmcstat_pcmap *map;
	struct pmcstat_image *image;
	struct pmcstat_gmonfile *pgf;
	uintfptr_t bucket;
	HISTCOUNTER *hc;
	WIDEHISTCOUNTER *whc;
	pmc_id_t pmcid;

	(void) nsamples; (void) usermode; (void) cpu;

	map = pmcstat_process_find_map(usermode ? pp : pmcstat_kernproc, cc[0]);
	if (map == NULL) {
		/* Unknown offset. */
		pmcstat_stats.ps_samples_unknown_offset++;
		return;
	}

	assert(cc[0] >= map->ppm_lowpc && cc[0] < map->ppm_highpc);

	image = map->ppm_image;
	pmcid = pmcr->pr_pmcid;

	/*
	 * If this is the first time we are seeing a sample for
	 * this executable image, try determine its parameters.
	 */
	if (image->pi_type == PMCSTAT_IMAGE_UNKNOWN)
		pmcstat_image_determine_type(image, &args);

	assert(image->pi_type != PMCSTAT_IMAGE_UNKNOWN);

	/* Ignore samples in images that we know nothing about. */
	if (image->pi_type == PMCSTAT_IMAGE_INDETERMINABLE) {
		pmcstat_stats.ps_samples_indeterminable++;
		return;
	}

	/*
	 * Find the gmon file corresponding to 'pmcid', creating it if
	 * needed.
	 */
	pgf = pmcstat_image_find_gmonfile(image, pmcid);
	if (pgf == NULL) {
		if (hc_sz == 0) {
			/* Determine the correct histcounter size. */
			if (args.pa_flags & FLAG_DO_WIDE_GPROF_HC)
				hc_sz = sizeof(WIDEHISTCOUNTER);
			else
				hc_sz = sizeof(HISTCOUNTER);
		}

		if ((pgf = calloc(1, sizeof(*pgf))) == NULL)
			err(EX_OSERR, "ERROR:");

		pgf->pgf_gmondata = NULL;	/* mark as unmapped */
		pgf->pgf_name = pmcstat_gmon_create_name(args.pa_samplesdir,
		    image, pmcid);
		pgf->pgf_pmcid = pmcid;
		assert(image->pi_end > image->pi_start);
		pgf->pgf_nbuckets = howmany(image->pi_end - image->pi_start,
		    FUNCTION_ALIGNMENT);	/* see <machine/profile.h> */
		pgf->pgf_ndatabytes = sizeof(struct gmonhdr) +
		    pgf->pgf_nbuckets * hc_sz;
		pgf->pgf_nsamples = 0;
		pgf->pgf_file = NULL;

		pmcstat_gmon_create_file(pgf, image);

		LIST_INSERT_HEAD(&image->pi_gmlist, pgf, pgf_next);
	}

	/*
	 * Map the gmon file in if needed.  It may have been mapped
	 * out under memory pressure.
	 */
	if (pgf->pgf_gmondata == NULL)
		pmcstat_gmon_map_file(pgf);

	assert(pgf->pgf_gmondata != NULL);

	/*
	 *
	 */

	bucket = (cc[0] - map->ppm_lowpc) / FUNCTION_ALIGNMENT;

	assert(bucket < pgf->pgf_nbuckets);

	if (args.pa_flags & FLAG_DO_WIDE_GPROF_HC) {
		whc = (WIDEHISTCOUNTER *) ((uintptr_t) pgf->pgf_gmondata +
		    sizeof(struct gmonhdr));

		/* saturating add */
		if (whc[bucket] < WIDEHISTCOUNTER_MAX)
			whc[bucket]++;
		else /* mark that an overflow occurred */
			pgf->pgf_overflow = 1;
	} else {
		hc = (HISTCOUNTER *) ((uintptr_t) pgf->pgf_gmondata +
		    sizeof(struct gmonhdr));

		/* saturating add */
		if (hc[bucket] < HISTCOUNTER_MAX)
			hc[bucket]++;
		else /* mark that an overflow occurred */
			pgf->pgf_overflow = 1;
	}

	pgf->pgf_nsamples++;
}

/*
 * Shutdown module.
 */

void
pmcpl_gmon_shutdown(FILE *mf)
{
	int i;
	struct pmcstat_gmonfile *pgf;
	struct pmcstat_image *pi;

	/*
	 * Sync back all gprof flat profile data.
	 */
	for (i = 0; i < PMCSTAT_NHASH; i++) {
		LIST_FOREACH(pi, &pmcstat_image_hash[i], pi_next) {
			if (mf)
				(void) fprintf(mf, " \"%s\" => \"%s\"",
				    pmcstat_string_unintern(pi->pi_execpath),
				    pmcstat_string_unintern(
				    pi->pi_samplename));

			/* flush gmon.out data to disk */
			LIST_FOREACH(pgf, &pi->pi_gmlist, pgf_next) {
				pmcstat_gmon_unmap_file(pgf);
				if (mf)
					(void) fprintf(mf, " %s/%d",
					    pmcstat_pmcid_to_name(
					    pgf->pgf_pmcid),
					    pgf->pgf_nsamples);
				if (pgf->pgf_overflow && args.pa_verbosity >= 1)
					warnx(
"WARNING: profile \"%s\" overflowed.",
					    pmcstat_string_unintern(
					        pgf->pgf_name));
			}

			if (mf)
				(void) fprintf(mf, "\n");
		}
	}

	/*
	 * Compute arcs and add these to the gprof files.
	 */
	if (args.pa_flags & FLAG_DO_GPROF && args.pa_graphdepth > 1)
		pmcstat_callgraph_do_gmon_arcs();
}
