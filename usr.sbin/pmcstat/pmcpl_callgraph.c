/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2007, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
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
#include "pmcstat_top.h"
#include "pmcpl_callgraph.h"

#define	min(A,B)		((A) < (B) ? (A) : (B))
#define	max(A,B)		((A) > (B) ? (A) : (B))

/* Get the sample value in percent related to nsamples. */
#define PMCPL_CG_COUNTP(a) \
	((a)->pcg_count * 100.0 / nsamples)

/*
 * The toplevel CG nodes (i.e., with rank == 0) are placed in a hash table.
 */

struct pmcstat_cgnode_hash_list pmcstat_cgnode_hash[PMCSTAT_NHASH];
int pmcstat_cgnode_hash_count;

static pmcstat_interned_string pmcstat_previous_filename_printed;

static struct pmcstat_cgnode *
pmcstat_cgnode_allocate(struct pmcstat_image *image, uintfptr_t pc)
{
	struct pmcstat_cgnode *cg;

	if ((cg = malloc(sizeof(*cg))) == NULL)
		err(EX_OSERR, "ERROR: Cannot allocate callgraph node");

	cg->pcg_image = image;
	cg->pcg_func = pc;

	cg->pcg_count = 0;
	cg->pcg_nchildren = 0;
	LIST_INIT(&cg->pcg_children);

	return (cg);
}

/*
 * Free a node and its children.
 */
static void
pmcstat_cgnode_free(struct pmcstat_cgnode *cg)
{
	struct pmcstat_cgnode *cgc, *cgtmp;

	LIST_FOREACH_SAFE(cgc, &cg->pcg_children, pcg_sibling, cgtmp)
		pmcstat_cgnode_free(cgc);
	free(cg);
}

/*
 * Look for a callgraph node associated with pmc `pmcid' in the global
 * hash table that corresponds to the given `pc' value in the process
 * `pp'.
 */
static struct pmcstat_cgnode *
pmcstat_cgnode_hash_lookup_pc(struct pmcstat_process *pp, pmc_id_t pmcid,
    uintfptr_t pc, int usermode)
{
	struct pmcstat_pcmap *ppm;
	struct pmcstat_symbol *sym;
	struct pmcstat_image *image;
	struct pmcstat_cgnode *cg;
	struct pmcstat_cgnode_hash *h;
	uintfptr_t loadaddress;
	unsigned int i, hash;

	ppm = pmcstat_process_find_map(usermode ? pp : pmcstat_kernproc, pc);
	if (ppm == NULL)
		return (NULL);

	image = ppm->ppm_image;

	loadaddress = ppm->ppm_lowpc + image->pi_vaddr - image->pi_start;
	pc -= loadaddress;	/* Convert to an offset in the image. */

	/*
	 * Try determine the function at this offset.  If we can't
	 * find a function round leave the `pc' value alone.
	 */
	if ((sym = pmcstat_symbol_search(image, pc)) != NULL)
		pc = sym->ps_start;
	else
		pmcstat_stats.ps_samples_unknown_function++;

	for (hash = i = 0; i < sizeof(uintfptr_t); i++)
		hash += (pc >> i) & 0xFF;

	hash &= PMCSTAT_HASH_MASK;

	cg = NULL;
	LIST_FOREACH(h, &pmcstat_cgnode_hash[hash], pch_next)
	{
		if (h->pch_pmcid != pmcid)
			continue;

		cg = h->pch_cgnode;

		assert(cg != NULL);

		if (cg->pcg_image == image && cg->pcg_func == pc)
			return (cg);
	}

	/*
	 * We haven't seen this (pmcid, pc) tuple yet, so allocate a
	 * new callgraph node and a new hash table entry for it.
	 */
	cg = pmcstat_cgnode_allocate(image, pc);
	if ((h = malloc(sizeof(*h))) == NULL)
		err(EX_OSERR, "ERROR: Could not allocate callgraph node");

	h->pch_pmcid = pmcid;
	h->pch_cgnode = cg;
	LIST_INSERT_HEAD(&pmcstat_cgnode_hash[hash], h, pch_next);

	pmcstat_cgnode_hash_count++;

	return (cg);
}

/*
 * Compare two callgraph nodes for sorting.
 */
static int
pmcstat_cgnode_compare(const void *a, const void *b)
{
	const struct pmcstat_cgnode *const *pcg1, *const *pcg2, *cg1, *cg2;

	pcg1 = (const struct pmcstat_cgnode *const *) a;
	cg1 = *pcg1;
	pcg2 = (const struct pmcstat_cgnode *const *) b;
	cg2 = *pcg2;

	/* Sort in reverse order */
	if (cg1->pcg_count < cg2->pcg_count)
		return (1);
	if (cg1->pcg_count > cg2->pcg_count)
		return (-1);
	return (0);
}

/*
 * Find (allocating if a needed) a callgraph node in the given
 * parent with the same (image, pcoffset) pair.
 */

static struct pmcstat_cgnode *
pmcstat_cgnode_find(struct pmcstat_cgnode *parent, struct pmcstat_image *image,
    uintfptr_t pcoffset)
{
	struct pmcstat_cgnode *child;

	LIST_FOREACH(child, &parent->pcg_children, pcg_sibling) {
		if (child->pcg_image == image &&
		    child->pcg_func == pcoffset)
			return (child);
	}

	/*
	 * Allocate a new structure.
	 */

	child = pmcstat_cgnode_allocate(image, pcoffset);

	/*
	 * Link it into the parent.
	 */
	LIST_INSERT_HEAD(&parent->pcg_children, child, pcg_sibling);
	parent->pcg_nchildren++;

	return (child);
}

/*
 * Print one callgraph node.  The output format is:
 *
 * indentation %(parent's samples) #nsamples function@object
 */
static void
pmcstat_cgnode_print(struct pmcstat_cgnode *cg, int depth, uint32_t total)
{
	uint32_t n;
	const char *space;
	struct pmcstat_symbol *sym;
	struct pmcstat_cgnode **sortbuffer, **cgn, *pcg;

	space = " ";

	if (depth > 0)
		(void) fprintf(args.pa_graphfile, "%*s", depth, space);

	if (cg->pcg_count == total)
		(void) fprintf(args.pa_graphfile, "100.0%% ");
	else
		(void) fprintf(args.pa_graphfile, "%05.2f%% ",
		    100.0 * cg->pcg_count / total);

	n = fprintf(args.pa_graphfile, " [%u] ", cg->pcg_count);

	/* #samples is a 12 character wide field. */
	if (n < 12)
		(void) fprintf(args.pa_graphfile, "%*s", 12 - n, space);

	if (depth > 0)
		(void) fprintf(args.pa_graphfile, "%*s", depth, space);

	sym = pmcstat_symbol_search(cg->pcg_image, cg->pcg_func);
	if (sym)
		(void) fprintf(args.pa_graphfile, "%s",
		    pmcstat_string_unintern(sym->ps_name));
	else
		(void) fprintf(args.pa_graphfile, "%p",
		    (void *) (cg->pcg_image->pi_vaddr + cg->pcg_func));

	if (pmcstat_previous_filename_printed !=
	    cg->pcg_image->pi_fullpath) {
		pmcstat_previous_filename_printed = cg->pcg_image->pi_fullpath;
		(void) fprintf(args.pa_graphfile, " @ %s\n",
		    pmcstat_string_unintern(
		    pmcstat_previous_filename_printed));
	} else
		(void) fprintf(args.pa_graphfile, "\n");

	if (cg->pcg_nchildren == 0)
		return;

	if ((sortbuffer = (struct pmcstat_cgnode **)
		malloc(sizeof(struct pmcstat_cgnode *) *
		    cg->pcg_nchildren)) == NULL)
		err(EX_OSERR, "ERROR: Cannot print callgraph");
	cgn = sortbuffer;

	LIST_FOREACH(pcg, &cg->pcg_children, pcg_sibling)
	    *cgn++ = pcg;

	assert(cgn - sortbuffer == (int) cg->pcg_nchildren);

	qsort(sortbuffer, cg->pcg_nchildren, sizeof(struct pmcstat_cgnode *),
	    pmcstat_cgnode_compare);

	for (cgn = sortbuffer, n = 0; n < cg->pcg_nchildren; n++, cgn++)
		pmcstat_cgnode_print(*cgn, depth+1, cg->pcg_count);

	free(sortbuffer);
}

/*
 * Record a callchain.
 */

void
pmcpl_cg_process(struct pmcstat_process *pp, struct pmcstat_pmcrecord *pmcr,
    uint32_t nsamples, uintfptr_t *cc, int usermode, uint32_t cpu)
{
	uintfptr_t pc, loadaddress;
	uint32_t n;
	struct pmcstat_image *image;
	struct pmcstat_pcmap *ppm;
	struct pmcstat_symbol *sym;
	struct pmcstat_cgnode *parent, *child;
	struct pmcstat_process *km;
	pmc_id_t pmcid;

	(void) cpu;

	/*
	 * Find the callgraph node recorded in the global hash table
	 * for this (pmcid, pc).
	 */

	pc = cc[0];
	pmcid = pmcr->pr_pmcid;
	child = parent = pmcstat_cgnode_hash_lookup_pc(pp, pmcid, pc, usermode);
	if (parent == NULL) {
		pmcstat_stats.ps_callchain_dubious_frames++;
		pmcr->pr_dubious_frames++;
		return;
	}

	parent->pcg_count++;

	/*
	 * For each return address in the call chain record, subject
	 * to the maximum depth desired.
	 * - Find the image associated with the sample.  Stop if there
	 *   there is no valid image at that address.
	 * - Find the function that overlaps the return address.
	 * - If found: use the start address of the function.
	 *   If not found (say an object's symbol table is not present or
	 *   is incomplete), round down to th gprof bucket granularity.
	 * - Convert return virtual address to an offset in the image.
	 * - Look for a child with the same {offset,image} tuple,
	 *   inserting one if needed.
	 * - Increment the count of occurrences of the child.
	 */
	km = pmcstat_kernproc;

	for (n = 1; n < (uint32_t) args.pa_graphdepth && n < nsamples; n++,
	    parent = child) {
		pc = cc[n];

		ppm = pmcstat_process_find_map(usermode ? pp : km, pc);
		if (ppm == NULL) {
			/* Detect full frame capture (kernel + user). */
			if (!usermode) {
				ppm = pmcstat_process_find_map(pp, pc);
				if (ppm != NULL)
					km = pp;
			}
		}
		if (ppm == NULL)
			continue;

		image = ppm->ppm_image;
		loadaddress = ppm->ppm_lowpc + image->pi_vaddr -
		    image->pi_start;
		pc -= loadaddress;

		if ((sym = pmcstat_symbol_search(image, pc)) != NULL)
			pc = sym->ps_start;

		child = pmcstat_cgnode_find(parent, image, pc);
		child->pcg_count++;
	}
}

/*
 * Printing a callgraph for a PMC.
 */
static void
pmcstat_callgraph_print_for_pmcid(struct pmcstat_pmcrecord *pmcr)
{
	int n, nentries;
	uint32_t nsamples;
	pmc_id_t pmcid;
	struct pmcstat_cgnode **sortbuffer, **cgn;
	struct pmcstat_cgnode_hash *pch;

	/*
	 * We pull out all callgraph nodes in the top-level hash table
	 * with a matching PMC id.  We then sort these based on the
	 * frequency of occurrence.  Each callgraph node is then
	 * printed.
	 */

	nsamples = 0;
	pmcid = pmcr->pr_pmcid;
	if ((sortbuffer = (struct pmcstat_cgnode **)
	    malloc(sizeof(struct pmcstat_cgnode *) *
	    pmcstat_cgnode_hash_count)) == NULL)
		err(EX_OSERR, "ERROR: Cannot sort callgraph");
	cgn = sortbuffer;

	for (n = 0; n < PMCSTAT_NHASH; n++)
		LIST_FOREACH(pch, &pmcstat_cgnode_hash[n], pch_next)
		    if (pch->pch_pmcid == pmcid) {
			    nsamples += pch->pch_cgnode->pcg_count;
			    *cgn++ = pch->pch_cgnode;
		    }

	nentries = cgn - sortbuffer;
	assert(nentries <= pmcstat_cgnode_hash_count);

	if (nentries == 0) {
		free(sortbuffer);
		return;
	}

	qsort(sortbuffer, nentries, sizeof(struct pmcstat_cgnode *),
	    pmcstat_cgnode_compare);

	(void) fprintf(args.pa_graphfile,
	    "@ %s [%u samples]\n\n",
	    pmcstat_string_unintern(pmcr->pr_pmcname),
	    nsamples);

	for (cgn = sortbuffer, n = 0; n < nentries; n++, cgn++) {
		pmcstat_previous_filename_printed = NULL;
		pmcstat_cgnode_print(*cgn, 0, nsamples);
		(void) fprintf(args.pa_graphfile, "\n");
	}

	free(sortbuffer);
}

/*
 * Print out callgraphs.
 */

static void
pmcstat_callgraph_print(void)
{
	struct pmcstat_pmcrecord *pmcr;

	LIST_FOREACH(pmcr, &pmcstat_pmcs, pr_next)
	    pmcstat_callgraph_print_for_pmcid(pmcr);
}

static void
pmcstat_cgnode_topprint(struct pmcstat_cgnode *cg,
    int depth __unused, uint32_t nsamples)
{
	int v_attrs, vs_len, ns_len, width, len, n, nchildren;
	float v;
	char ns[30], vs[10];
	struct pmcstat_symbol *sym;
	struct pmcstat_cgnode **sortbuffer, **cgn, *pcg;

	/* Format value. */
	v = PMCPL_CG_COUNTP(cg);
	snprintf(vs, sizeof(vs), "%.1f", v);
	v_attrs = PMCSTAT_ATTRPERCENT(v);
	sym = NULL;

	/* Format name. */
	if (!(args.pa_flags & FLAG_SKIP_TOP_FN_RES))
		sym = pmcstat_symbol_search(cg->pcg_image, cg->pcg_func);
	if (sym != NULL) {
		snprintf(ns, sizeof(ns), "%s",
		    pmcstat_string_unintern(sym->ps_name));
	} else
		snprintf(ns, sizeof(ns), "%p",
		    (void *)(cg->pcg_image->pi_vaddr + cg->pcg_func));

	PMCSTAT_ATTRON(v_attrs);
	PMCSTAT_PRINTW("%5.5s", vs);
	PMCSTAT_ATTROFF(v_attrs);
	PMCSTAT_PRINTW(" %-10.10s %-20.20s",
	    pmcstat_string_unintern(cg->pcg_image->pi_name),
	    ns);

	nchildren = cg->pcg_nchildren;
	if (nchildren == 0) {
		PMCSTAT_PRINTW("\n");
		return;
	}

	width = pmcstat_displaywidth - 40;

	if ((sortbuffer = (struct pmcstat_cgnode **)
		malloc(sizeof(struct pmcstat_cgnode *) *
		    nchildren)) == NULL)
		err(EX_OSERR, "ERROR: Cannot print callgraph");
	cgn = sortbuffer;

	LIST_FOREACH(pcg, &cg->pcg_children, pcg_sibling)
	    *cgn++ = pcg;

	assert(cgn - sortbuffer == (int)nchildren);

	qsort(sortbuffer, nchildren, sizeof(struct pmcstat_cgnode *),
	    pmcstat_cgnode_compare);

	/* Count how many callers. */
	for (cgn = sortbuffer, n = 0; n < nchildren; n++, cgn++) {
		pcg = *cgn;

		v = PMCPL_CG_COUNTP(pcg);
		if (v < pmcstat_threshold)
			break;
	}
	nchildren = n;

	for (cgn = sortbuffer, n = 0; n < nchildren; n++, cgn++) {
		pcg = *cgn;

		/* Format value. */
		if (nchildren > 1) {
			v = PMCPL_CG_COUNTP(pcg);
			vs_len = snprintf(vs, sizeof(vs), ":%.1f", v);
			v_attrs = PMCSTAT_ATTRPERCENT(v);
		} else
			vs_len = 0;

		/* Format name. */
		sym = pmcstat_symbol_search(pcg->pcg_image, pcg->pcg_func);
		if (sym != NULL) {
			ns_len = snprintf(ns, sizeof(ns), "%s",
			    pmcstat_string_unintern(sym->ps_name));
		} else
			ns_len = snprintf(ns, sizeof(ns), "%p",
			    (void *)pcg->pcg_func);

		len = ns_len + vs_len + 1;
		if (width - len < 0) {
			PMCSTAT_PRINTW(" ...");
			break;
		}
		width -= len;

		PMCSTAT_PRINTW(" %s", ns);
		if (nchildren > 1) {
			PMCSTAT_ATTRON(v_attrs);
			PMCSTAT_PRINTW("%s", vs);
			PMCSTAT_ATTROFF(v_attrs);
		}
	}
	PMCSTAT_PRINTW("\n");
	free(sortbuffer);
}

/*
 * Top mode display.
 */

void
pmcpl_cg_topdisplay(void)
{
	int n, nentries;
	uint32_t nsamples;
	struct pmcstat_cgnode **sortbuffer, **cgn;
	struct pmcstat_cgnode_hash *pch;
	struct pmcstat_pmcrecord *pmcr;

	pmcr = pmcstat_pmcindex_to_pmcr(pmcstat_pmcinfilter);
	if (!pmcr)
		err(EX_SOFTWARE, "ERROR: invalid pmcindex");

	/*
	 * We pull out all callgraph nodes in the top-level hash table
	 * with a matching PMC index.  We then sort these based on the
	 * frequency of occurrence.  Each callgraph node is then
	 * printed.
	 */

	nsamples = 0;

	if ((sortbuffer = (struct pmcstat_cgnode **)
	    malloc(sizeof(struct pmcstat_cgnode *) *
	    pmcstat_cgnode_hash_count)) == NULL)
		err(EX_OSERR, "ERROR: Cannot sort callgraph");
	cgn = sortbuffer;

	for (n = 0; n < PMCSTAT_NHASH; n++)
		LIST_FOREACH(pch, &pmcstat_cgnode_hash[n], pch_next)
		    if (pmcr == NULL || pch->pch_pmcid == pmcr->pr_pmcid) {
			    nsamples += pch->pch_cgnode->pcg_count;
			    *cgn++ = pch->pch_cgnode;
		    }

	nentries = cgn - sortbuffer;
	assert(nentries <= pmcstat_cgnode_hash_count);

	if (nentries == 0) {
		free(sortbuffer);
		return;
	}

	qsort(sortbuffer, nentries, sizeof(struct pmcstat_cgnode *),
	    pmcstat_cgnode_compare);

	PMCSTAT_PRINTW("%5.5s %-10.10s %-20.20s %s\n",
	    "%SAMP", "IMAGE", "FUNCTION", "CALLERS");

	nentries = min(pmcstat_displayheight - 2, nentries);

	for (cgn = sortbuffer, n = 0; n < nentries; n++, cgn++) {
		if (PMCPL_CG_COUNTP(*cgn) < pmcstat_threshold)
			break;
		pmcstat_cgnode_topprint(*cgn, 0, nsamples);
	}

	free(sortbuffer);
}

/*
 * Handle top mode keypress.
 */

int
pmcpl_cg_topkeypress(int c, void *arg)
{
	WINDOW *w;

	w = (WINDOW *)arg;

	(void) c; (void) w;

	return 0;
}

int
pmcpl_cg_init(void)
{
	int i;

	pmcstat_cgnode_hash_count = 0;
	pmcstat_previous_filename_printed = NULL;

	for (i = 0; i < PMCSTAT_NHASH; i++) {
		LIST_INIT(&pmcstat_cgnode_hash[i]);
	}

	return (0);
}

void
pmcpl_cg_shutdown(FILE *mf)
{
	int i;
	struct pmcstat_cgnode_hash *pch, *pchtmp;

	(void) mf;

	if (args.pa_flags & FLAG_DO_CALLGRAPHS)
		pmcstat_callgraph_print();

	/*
	 * Free memory.
	 */
	for (i = 0; i < PMCSTAT_NHASH; i++) {
		LIST_FOREACH_SAFE(pch, &pmcstat_cgnode_hash[i], pch_next,
		    pchtmp) {
			pmcstat_cgnode_free(pch->pch_cgnode);
			LIST_REMOVE(pch, pch_next);
			free(pch);
		}
	}
}

