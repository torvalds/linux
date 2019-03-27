/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, Fabien Thomas
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

/*
 * Process hwpmc(4) samples as calltree.
 *
 * Output file format compatible with Kcachegrind (kdesdk).
 * Handle top mode with a sorted tree display.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/queue.h>

#include <assert.h>
#include <curses.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pmc.h>
#include <pmclog.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

#include "pmcstat.h"
#include "pmcstat_log.h"
#include "pmcstat_top.h"
#include "pmcpl_calltree.h"

#define	min(A,B)		((A) < (B) ? (A) : (B))
#define	max(A,B)		((A) > (B) ? (A) : (B))

#define	PMCPL_CT_GROWSIZE	4

static int pmcstat_skiplink = 0;

struct pmcpl_ct_node;

/* Get the sample value for PMC a. */
#define	PMCPL_CT_SAMPLE(a, b) \
	((a) < (b)->npmcs ? (b)->sb[a] : 0)

/* Get the sample value in percent related to rsamples. */
#define	PMCPL_CT_SAMPLEP(a, b) \
	(PMCPL_CT_SAMPLE(a, b) * 100.0 / rsamples->sb[a])

struct pmcpl_ct_sample {
	int		npmcs;		/* Max pmc index available. */
	unsigned	*sb;		/* Sample buffer for 0..npmcs. */
};

struct pmcpl_ct_arc {
	struct pmcpl_ct_sample	pcta_samples;
	struct pmcpl_ct_sample	pcta_callid;
	unsigned		pcta_call;
	struct pmcpl_ct_node	*pcta_child;
};

struct pmcpl_ct_instr {
	uintfptr_t		pctf_func;
	struct pmcpl_ct_sample	pctf_samples;
};

/*
 * Each calltree node is tracked by a pmcpl_ct_node struct.
 */
struct pmcpl_ct_node {
	struct pmcstat_image	*pct_image;
	uintfptr_t		pct_func;

	struct pmcstat_symbol	*pct_sym;
	pmcstat_interned_string	pct_ifl;
	pmcstat_interned_string	pct_ifn;

	struct pmcpl_ct_sample	pct_samples;

	int			pct_narc;
	int			pct_arc_c;
	struct pmcpl_ct_arc 	*pct_arc;

	/* TODO: optimize for large number of items. */
	int			pct_ninstr;
	int			pct_instr_c;
	struct pmcpl_ct_instr	*pct_instr;

#define PMCPL_PCT_ADDR	0
#define PMCPL_PCT_NAME	1
	char			pct_type;
#define	PMCPL_PCT_WHITE	0
#define	PMCPL_PCT_GREY	1
#define	PMCPL_PCT_BLACK	2
	char			pct_color;
};

struct pmcpl_ct_node_hash {
	struct pmcpl_ct_node  *pch_ctnode;
	STAILQ_ENTRY(pmcpl_ct_node_hash) pch_next;
};

static struct pmcpl_ct_sample pmcpl_ct_callid;

#define	PMCPL_CT_MAXCOL		PMC_CALLCHAIN_DEPTH_MAX
#define	PMCPL_CT_MAXLINE	1024	/* TODO: dynamic. */

struct pmcpl_ct_line {
	unsigned	ln_sum;
	unsigned	ln_index;
};

static struct pmcpl_ct_line	pmcpl_ct_topmax[PMCPL_CT_MAXLINE+1];
static struct pmcpl_ct_node
    *pmcpl_ct_topscreen[PMCPL_CT_MAXCOL+1][PMCPL_CT_MAXLINE+1];

/*
 * All nodes indexed by function/image name are placed in a hash table.
 */
static STAILQ_HEAD(,pmcpl_ct_node_hash) pmcpl_ct_node_hash[PMCSTAT_NHASH];

/*
 * Root node for the graph.
 */
static struct pmcpl_ct_node *pmcpl_ct_root;

/*
 * Prototypes
 */

/*
 * Initialize a samples.
 */

static void
pmcpl_ct_samples_init(struct pmcpl_ct_sample *samples)
{

	samples->npmcs = 0;
	samples->sb = NULL;
}

/*
 * Free a samples.
 */

static void
pmcpl_ct_samples_free(struct pmcpl_ct_sample *samples)
{

	samples->npmcs = 0;
	free(samples->sb);
	samples->sb = NULL;
}

/*
 * Grow a sample block to store pmcstat_npmcs PMCs.
 */

static void
pmcpl_ct_samples_grow(struct pmcpl_ct_sample *samples)
{
	unsigned int npmcs;

	/* Enough storage. */
	if (pmcstat_npmcs <= samples->npmcs)
                return;

	npmcs = samples->npmcs +
	    max(pmcstat_npmcs - samples->npmcs, PMCPL_CT_GROWSIZE);
	samples->sb = reallocarray(samples->sb, npmcs, sizeof(unsigned));
	if (samples->sb == NULL)
		errx(EX_SOFTWARE, "ERROR: out of memory");
	bzero((char *)samples->sb + samples->npmcs * sizeof(unsigned),
	    (npmcs - samples->npmcs) * sizeof(unsigned));
	samples->npmcs = npmcs;
}

/*
 * Compute the sum of all root arcs.
 */

static void
pmcpl_ct_samples_root(struct pmcpl_ct_sample *samples)
{
	int i, pmcin;

	pmcpl_ct_samples_init(samples);
	pmcpl_ct_samples_grow(samples);

	for (i = 0; i < pmcpl_ct_root->pct_narc; i++)
		for (pmcin = 0; pmcin < pmcstat_npmcs; pmcin++)
			samples->sb[pmcin] += PMCPL_CT_SAMPLE(pmcin,
			    &pmcpl_ct_root->pct_arc[i].pcta_samples);
}

/*
 * Grow the arc table.
 */

static void
pmcpl_ct_arc_grow(int cursize, int *maxsize, struct pmcpl_ct_arc **items)
{
	unsigned int nmaxsize;

	if (cursize < *maxsize)
		return;

	nmaxsize = *maxsize + max(cursize + 1 - *maxsize, PMCPL_CT_GROWSIZE);
	*items = reallocarray(*items, nmaxsize, sizeof(struct pmcpl_ct_arc));
	if (*items == NULL)
		errx(EX_SOFTWARE, "ERROR: out of memory");
	bzero((char *)*items + *maxsize * sizeof(struct pmcpl_ct_arc),
	    (nmaxsize - *maxsize) * sizeof(struct pmcpl_ct_arc));
	*maxsize = nmaxsize;
}

/*
 * Grow the instr table.
 */

static void
pmcpl_ct_instr_grow(int cursize, int *maxsize, struct pmcpl_ct_instr **items)
{
	unsigned int nmaxsize;

	if (cursize < *maxsize)
		return;

	nmaxsize = *maxsize + max(cursize + 1 - *maxsize, PMCPL_CT_GROWSIZE);
	*items = reallocarray(*items, nmaxsize, sizeof(struct pmcpl_ct_instr));
	if (*items == NULL)
		errx(EX_SOFTWARE, "ERROR: out of memory");
	bzero((char *)*items + *maxsize * sizeof(struct pmcpl_ct_instr),
	    (nmaxsize - *maxsize) * sizeof(struct pmcpl_ct_instr));
	*maxsize = nmaxsize;
}

/*
 * Add a new instruction sample to given node.
 */

static void
pmcpl_ct_instr_add(struct pmcpl_ct_node *ct, int pmcin,
    uintfptr_t pc, unsigned v)
{
	int i;
	struct pmcpl_ct_instr *in;

	for (i = 0; i<ct->pct_ninstr; i++) {
		if (ct->pct_instr[i].pctf_func == pc) {
			in = &ct->pct_instr[i];
			pmcpl_ct_samples_grow(&in->pctf_samples);
			in->pctf_samples.sb[pmcin] += v;
			return;
		}
	}

	pmcpl_ct_instr_grow(ct->pct_ninstr, &ct->pct_instr_c, &ct->pct_instr);
	in = &ct->pct_instr[ct->pct_ninstr];
	in->pctf_func = pc;
	pmcpl_ct_samples_init(&in->pctf_samples);
	pmcpl_ct_samples_grow(&in->pctf_samples);
	in->pctf_samples.sb[pmcin] = v;
	ct->pct_ninstr++;
}

/*
 * Allocate a new node.
 */

static struct pmcpl_ct_node *
pmcpl_ct_node_allocate(void)
{
	struct pmcpl_ct_node *ct;

	if ((ct = malloc(sizeof(*ct))) == NULL)
		err(EX_OSERR, "ERROR: Cannot allocate callgraph node");

	pmcpl_ct_samples_init(&ct->pct_samples);

	ct->pct_sym	= NULL;
	ct->pct_image	= NULL;
	ct->pct_func	= 0;

	ct->pct_narc	= 0;
	ct->pct_arc_c	= 0;
	ct->pct_arc	= NULL;

	ct->pct_ninstr	= 0;
	ct->pct_instr_c	= 0;
	ct->pct_instr	= NULL;

	ct->pct_color   = PMCPL_PCT_WHITE;

	return (ct);
}

/*
 * Free a node.
 */

static void
pmcpl_ct_node_free(struct pmcpl_ct_node *ct)
{
	int i;

	for (i = 0; i < ct->pct_narc; i++) {
		pmcpl_ct_samples_free(&ct->pct_arc[i].pcta_samples);
		pmcpl_ct_samples_free(&ct->pct_arc[i].pcta_callid);
	}

	pmcpl_ct_samples_free(&ct->pct_samples);
	free(ct->pct_arc);
	free(ct->pct_instr);
	free(ct);
}

/*
 * Clear the graph tag on each node.
 */
static void
pmcpl_ct_node_cleartag(void)
{
	int i;
	struct pmcpl_ct_node_hash *pch;

	for (i = 0; i < PMCSTAT_NHASH; i++)
		STAILQ_FOREACH(pch, &pmcpl_ct_node_hash[i], pch_next)
			pch->pch_ctnode->pct_color = PMCPL_PCT_WHITE;

	pmcpl_ct_root->pct_color = PMCPL_PCT_WHITE;
}

/*
 * Print the callchain line by line with maximum cost at top.
 */ 

static int
pmcpl_ct_node_dumptop(int pmcin, struct pmcpl_ct_node *ct,
    struct pmcpl_ct_sample *rsamples, int x, int *y)
{
	int i, terminal;
	struct pmcpl_ct_arc *arc;

	if (ct->pct_color == PMCPL_PCT_GREY)
		return 0;

	if (x >= PMCPL_CT_MAXCOL) {
		pmcpl_ct_topscreen[x][*y] = NULL;
		return 1;
	}
	pmcpl_ct_topscreen[x][*y] = ct;

	/*
	 * Check if this is a terminal node.
	 * We need to check that some samples exist
	 * for at least one arc for that PMC.
	 */
	terminal = 1;
	for (i = 0; i < ct->pct_narc; i++) {
		arc = &ct->pct_arc[i];
		if (arc->pcta_child->pct_color != PMCPL_PCT_GREY &&
		    PMCPL_CT_SAMPLE(pmcin,
		    &arc->pcta_samples) != 0 &&
		    PMCPL_CT_SAMPLEP(pmcin,
		    &arc->pcta_samples) > pmcstat_threshold) {
			terminal = 0;
			break;
		}
	}

	if (ct->pct_narc == 0 || terminal) {
		pmcpl_ct_topscreen[x+1][*y] = NULL;
		if (*y >= PMCPL_CT_MAXLINE)
			return 1;
		*y = *y + 1;
		for (i=0; i < x; i++)
			pmcpl_ct_topscreen[i][*y] =
			    pmcpl_ct_topscreen[i][*y - 1];
		return 0;
	}

	ct->pct_color = PMCPL_PCT_GREY;
	for (i = 0; i < ct->pct_narc; i++) {
		if (PMCPL_CT_SAMPLE(pmcin,
		    &ct->pct_arc[i].pcta_samples) == 0)
			continue;
		if (PMCPL_CT_SAMPLEP(pmcin,
		    &ct->pct_arc[i].pcta_samples) > pmcstat_threshold) {
			if (pmcpl_ct_node_dumptop(pmcin,
			        ct->pct_arc[i].pcta_child,
			        rsamples, x+1, y)) {
				ct->pct_color = PMCPL_PCT_BLACK;
				return 1;
			}
		}
	}
	ct->pct_color = PMCPL_PCT_BLACK;

	return 0;
}

/*
 * Compare two top line by sum.
 */
static int
pmcpl_ct_line_compare(const void *a, const void *b)
{
	const struct pmcpl_ct_line *ct1, *ct2;

	ct1 = (const struct pmcpl_ct_line *) a;
	ct2 = (const struct pmcpl_ct_line *) b;

	/* Sort in reverse order */
	if (ct1->ln_sum < ct2->ln_sum)
		return (1);
	if (ct1->ln_sum > ct2->ln_sum)
		return (-1);
	return (0);
}

/*
 * Format and display given PMC index.
 */

static void
pmcpl_ct_node_printtop(struct pmcpl_ct_sample *rsamples, int pmcin, int maxy)
{
#undef	TS
#undef	TSI
#define	TS(x, y)	(pmcpl_ct_topscreen[x][y])
#define	TSI(x, y)	(pmcpl_ct_topscreen[x][pmcpl_ct_topmax[y].ln_index])

	int v_attrs, ns_len, vs_len, is_len, width, indentwidth, x, y;
	float v;
	char ns[30], vs[10], is[20];
	struct pmcpl_ct_node *ct;
	const char *space = " ";

	/*
	 * Sort by line cost.
	 */
	for (y = 0; ; y++) {
		ct = TS(1, y);
		if (ct == NULL)
			break;

		pmcpl_ct_topmax[y].ln_sum = 0;
		pmcpl_ct_topmax[y].ln_index = y;
		for (x = 1; TS(x, y) != NULL; x++) {
			pmcpl_ct_topmax[y].ln_sum +=
			    PMCPL_CT_SAMPLE(pmcin, &TS(x, y)->pct_samples);
		}
	}
	qsort(pmcpl_ct_topmax, y, sizeof(pmcpl_ct_topmax[0]),
	    pmcpl_ct_line_compare);
	pmcpl_ct_topmax[y].ln_index = y;

	for (y = 0; y < maxy; y++) {
		ct = TSI(1, y);
		if (ct == NULL)
			break;

		if (y > 0)
			PMCSTAT_PRINTW("\n");

		/* Output sum. */
		v = pmcpl_ct_topmax[y].ln_sum * 100.0 /
		    rsamples->sb[pmcin];
		snprintf(vs, sizeof(vs), "%.1f", v);
		v_attrs = PMCSTAT_ATTRPERCENT(v);
		PMCSTAT_ATTRON(v_attrs);
		PMCSTAT_PRINTW("%5.5s ", vs);
		PMCSTAT_ATTROFF(v_attrs);

		width = indentwidth = 5 + 1;

		for (x = 1; (ct = TSI(x, y)) != NULL; x++) {

			vs[0] = '\0'; vs_len = 0;
			is[0] = '\0'; is_len = 0;

			/* Format value. */
			v = PMCPL_CT_SAMPLEP(pmcin, &ct->pct_samples);
			if (v > pmcstat_threshold)
				vs_len  = snprintf(vs, sizeof(vs),
				    "(%.1f%%)", v);
			v_attrs = PMCSTAT_ATTRPERCENT(v);

			if (pmcstat_skiplink && v <= pmcstat_threshold) {
				strlcpy(ns, ".", sizeof(ns));
				ns_len = 1;
			} else {
			if (ct->pct_sym != NULL) {
				ns_len = snprintf(ns, sizeof(ns), "%s",
				    pmcstat_string_unintern(ct->pct_sym->ps_name));
			} else
				ns_len = snprintf(ns, sizeof(ns), "%p",
				    (void *)ct->pct_func);

			/* Format image. */
			if (x == 1 ||
			    TSI(x-1, y)->pct_image != ct->pct_image)
				is_len = snprintf(is, sizeof(is), "@%s",
				    pmcstat_string_unintern(ct->pct_image->pi_name));

			/* Check for line wrap. */
			width += ns_len + is_len + vs_len + 1;
			}
			if (width >= pmcstat_displaywidth) {
				maxy--;
				if (y >= maxy)
					break;
				PMCSTAT_PRINTW("\n%*s", indentwidth, space);
				width = indentwidth + ns_len + is_len + vs_len;
			}

			PMCSTAT_ATTRON(v_attrs);
			PMCSTAT_PRINTW("%s%s%s ", ns, is, vs);
			PMCSTAT_ATTROFF(v_attrs);
		}
	}
}

/*
 * Output top mode snapshot.
 */

void
pmcpl_ct_topdisplay(void)
{
	int y;
	struct pmcpl_ct_sample r, *rsamples;

	rsamples = &r;
	pmcpl_ct_samples_root(rsamples);
	pmcpl_ct_node_cleartag();

	PMCSTAT_PRINTW("%5.5s %s\n", "%SAMP", "CALLTREE");

	y = 0;
	if (pmcpl_ct_node_dumptop(pmcstat_pmcinfilter,
	    pmcpl_ct_root, rsamples, 0, &y))
		PMCSTAT_PRINTW("...\n");
	pmcpl_ct_topscreen[1][y] = NULL;

	pmcpl_ct_node_printtop(rsamples,
	    pmcstat_pmcinfilter, pmcstat_displayheight - 2);

	pmcpl_ct_samples_free(rsamples);
}

/*
 * Handle top mode keypress.
 */

int
pmcpl_ct_topkeypress(int c, void *arg)
{
	WINDOW *w;

	w = (WINDOW *)arg;

	switch (c) {
	case 'f':
		pmcstat_skiplink = !pmcstat_skiplink;
		wprintw(w, "skip empty link %s",
		    pmcstat_skiplink ? "on" : "off");
		break;
	}

	return 0;
}

/*
 * Look for a callgraph node associated with pmc `pmcid' in the global
 * hash table that corresponds to the given `pc' value in the process map
 * `ppm'.
 */

static void
pmcpl_ct_node_update(struct pmcpl_ct_node *parent,
    struct pmcpl_ct_node *child, int pmcin, unsigned v, int cd)
{
	struct pmcpl_ct_arc *arc;
	int i;

	assert(parent != NULL);

	/*
	 * Find related arc in parent node and
	 * increment the sample count.
	 */
	for (i = 0; i < parent->pct_narc; i++) {
		if (parent->pct_arc[i].pcta_child == child) {
			arc = &parent->pct_arc[i];
			pmcpl_ct_samples_grow(&arc->pcta_samples);
			arc->pcta_samples.sb[pmcin] += v;
			/* Estimate call count. */
			if (cd) {
			pmcpl_ct_samples_grow(&arc->pcta_callid);
			if (pmcpl_ct_callid.sb[pmcin] -
			    arc->pcta_callid.sb[pmcin] > 1)
				arc->pcta_call++;
			arc->pcta_callid.sb[pmcin] =
			    pmcpl_ct_callid.sb[pmcin];
			}
			return;
		}
	}

	/*
	 * No arc found for us, add ourself to the parent.
	 */
	pmcpl_ct_arc_grow(parent->pct_narc,
	    &parent->pct_arc_c, &parent->pct_arc);
	arc = &parent->pct_arc[parent->pct_narc];
	pmcpl_ct_samples_grow(&arc->pcta_samples);
	arc->pcta_samples.sb[pmcin] = v;
	arc->pcta_call = 1;
	if (cd) {
		pmcpl_ct_samples_grow(&arc->pcta_callid);
		arc->pcta_callid.sb[pmcin] = pmcpl_ct_callid.sb[pmcin];
	}
	arc->pcta_child = child;
	parent->pct_narc++;
}

/*
 * Lookup by image/pc.
 */

static struct pmcpl_ct_node *
pmcpl_ct_node_hash_lookup(struct pmcstat_image *image, uintfptr_t pc,
    struct pmcstat_symbol *sym, char *fl, char *fn)
{
	int i;
	unsigned int hash;
	struct pmcpl_ct_node *ct;
	struct pmcpl_ct_node_hash *h;
	pmcstat_interned_string	ifl, ifn;

	if (fn != NULL) {
		ifl = pmcstat_string_intern(fl);
		ifn = pmcstat_string_intern(fn);
	} else {
		ifl = 0;
		ifn = 0;
	}

	for (hash = i = 0; i < (int)sizeof(uintfptr_t); i++)
		hash += (pc >> i) & 0xFF;

	hash &= PMCSTAT_HASH_MASK;

	STAILQ_FOREACH(h, &pmcpl_ct_node_hash[hash], pch_next) {
		ct = h->pch_ctnode;

		assert(ct != NULL);

		if (ct->pct_image == image && ct->pct_func == pc) {
			if (fn == NULL)
				return (ct);
			if (ct->pct_type == PMCPL_PCT_NAME &&
			    ct->pct_ifl == ifl && ct->pct_ifn == ifn)
				return (ct);
		}
	}

	/*
	 * We haven't seen this (pmcid, pc) tuple yet, so allocate a
	 * new callgraph node and a new hash table entry for it.
	 */
	ct = pmcpl_ct_node_allocate();
	if ((h = malloc(sizeof(*h))) == NULL)
		err(EX_OSERR, "ERROR: Could not allocate callgraph node");

	if (fn != NULL) {
		ct->pct_type = PMCPL_PCT_NAME;
		ct->pct_ifl = ifl;
		ct->pct_ifn = ifn;
	} else
		ct->pct_type = PMCPL_PCT_ADDR;
	ct->pct_image = image;
	ct->pct_func = pc;
	ct->pct_sym = sym;

	h->pch_ctnode = ct;
	STAILQ_INSERT_HEAD(&pmcpl_ct_node_hash[hash], h, pch_next);
	return (ct);
}

/*
 * Record a callchain.
 */

void
pmcpl_ct_process(struct pmcstat_process *pp, struct pmcstat_pmcrecord *pmcr,
    uint32_t nsamples, uintfptr_t *cc, int usermode, uint32_t cpu)
{
	int i, n, pmcin;
	uintfptr_t pc, loadaddress;
	struct pmcstat_image *image;
	struct pmcstat_symbol *sym;
	struct pmcstat_pcmap *ppm[PMC_CALLCHAIN_DEPTH_MAX];
	struct pmcstat_process *km;
	struct pmcpl_ct_node *ct;
	struct pmcpl_ct_node *ctl[PMC_CALLCHAIN_DEPTH_MAX+1];

	(void) cpu;

	assert(nsamples>0 && nsamples<=PMC_CALLCHAIN_DEPTH_MAX);

	/* Get the PMC index. */
	pmcin = pmcr->pr_pmcin;

	/*
	 * Validate mapping for the callchain.
	 * Go from bottom to first invalid entry.
	 */
	km = pmcstat_kernproc;
	for (n = 0; n < (int)nsamples; n++) {
		ppm[n] = pmcstat_process_find_map(usermode ?
		    pp : km, cc[n]);
		if (ppm[n] == NULL) {
			/* Detect full frame capture (kernel + user). */
			if (!usermode) {
				ppm[n] = pmcstat_process_find_map(pp, cc[n]);
				if (ppm[n] != NULL)
					km = pp;
			}
		}
		if (ppm[n] == NULL)
			break;
	}
	if (n-- == 0) {
		pmcstat_stats.ps_callchain_dubious_frames++;
		pmcr->pr_dubious_frames++;
		return;
	}

	/* Increase the call generation counter. */
	pmcpl_ct_samples_grow(&pmcpl_ct_callid);
	pmcpl_ct_callid.sb[pmcin]++;

	/*
	 * Build node list.
	 */
	ctl[0] = pmcpl_ct_root;
	for (i = 1; n >= 0; n--) {
		image = ppm[n]->ppm_image;
		loadaddress = ppm[n]->ppm_lowpc +
		    image->pi_vaddr - image->pi_start;
		/* Convert to an offset in the image. */
		pc = cc[n] - loadaddress;
		/*
		 * Try determine the function at this offset.  If we can't
		 * find a function round leave the `pc' value alone.
		 */
		if ((sym = pmcstat_symbol_search(image, pc)) != NULL)
			pc = sym->ps_start;
		else
			pmcstat_stats.ps_samples_unknown_function++;

		ct = pmcpl_ct_node_hash_lookup(image, pc, sym, NULL, NULL);
		if (ct == NULL) {
			pmcstat_stats.ps_callchain_dubious_frames++;
			continue;
		}
		ctl[i++] = ct;
	}
	/* No valid node found. */
	if (i == 1)
		return;
	n = i;

	ct = ctl[0];
	for (i = 1; i < n; i++)
		pmcpl_ct_node_update(ctl[i-1], ctl[i], pmcin, 1, 1);

	/*
	 * Increment the sample count for this PMC.
	 */
	pmcpl_ct_samples_grow(&ctl[n-1]->pct_samples);
	ctl[n-1]->pct_samples.sb[pmcin]++;

	/* Update per instruction sample if required. */
	if (args.pa_ctdumpinstr)
		pmcpl_ct_instr_add(ctl[n-1], pmcin, cc[0] -
		    (ppm[0]->ppm_lowpc + ppm[0]->ppm_image->pi_vaddr -
		     ppm[0]->ppm_image->pi_start), 1);
}

/*
 * Print node child cost.
 */

static void
pmcpl_ct_node_printchild(struct pmcpl_ct_node *ct, uintfptr_t paddr,
    int pline)
{
	int i, j, line;
	uintfptr_t addr;
	struct pmcpl_ct_node *child;
	char sourcefile[PATH_MAX];
	char funcname[PATH_MAX];

	/*
	 * Child cost.
	 * TODO: attach child cost to the real position in the function.
	 * TODO: cfn=<fn> / call <ncall> addr(<fn>) / addr(call <fn>) <arccost>
	 */
	for (i=0 ; i<ct->pct_narc; i++) {
		child = ct->pct_arc[i].pcta_child;
		/* Object binary. */
		fprintf(args.pa_graphfile, "cob=%s\n",
		    pmcstat_string_unintern(child->pct_image->pi_fullpath));
		/* Child function name. */
		addr = child->pct_image->pi_vaddr + child->pct_func;
		line = 0;
		/* Child function source file. */
		if (child->pct_type == PMCPL_PCT_NAME) {
			fprintf(args.pa_graphfile, "cfi=%s\ncfn=%s\n",
			    pmcstat_string_unintern(child->pct_ifl),
			    pmcstat_string_unintern(child->pct_ifn));
		} else if (pmcstat_image_addr2line(child->pct_image, addr,
		    sourcefile, sizeof(sourcefile), &line,
		    funcname, sizeof(funcname))) {
			fprintf(args.pa_graphfile, "cfi=%s\ncfn=%s\n",
				sourcefile, funcname);
		} else {
			if (child->pct_sym != NULL)
				fprintf(args.pa_graphfile,
				    "cfi=???\ncfn=%s\n",
				    pmcstat_string_unintern(
				        child->pct_sym->ps_name));
			else
				fprintf(args.pa_graphfile,
				    "cfi=???\ncfn=%p\n", (void *)addr);
		}

		/* Child function address, line and call count. */
		fprintf(args.pa_graphfile, "calls=%u %p %u\n",
		    ct->pct_arc[i].pcta_call, (void *)addr, line);

		/*
		 * Call address, line, sample.
		 * TODO: Associate call address to the right location.
		 */
		fprintf(args.pa_graphfile, "%p %u", (void *)paddr, pline);
		for (j = 0; j<pmcstat_npmcs; j++)
			fprintf(args.pa_graphfile, " %u",
			    PMCPL_CT_SAMPLE(j, &ct->pct_arc[i].pcta_samples));
		fprintf(args.pa_graphfile, "\n");
	}
}

/*
 * Print node self cost.
 */

static void
pmcpl_ct_node_printself(struct pmcpl_ct_node *ct)
{
	int i, j, fline, line;
	uintfptr_t faddr, addr;
	char sourcefile[PATH_MAX];
	char funcname[PATH_MAX];

	/*
	 * Object binary.
	 */
	fprintf(args.pa_graphfile, "ob=%s\n",
	    pmcstat_string_unintern(ct->pct_image->pi_fullpath));

	/*
	 * Function name.
	 */
	faddr = ct->pct_image->pi_vaddr + ct->pct_func;
	fline = 0;
	if (ct->pct_type == PMCPL_PCT_NAME) {
		fprintf(args.pa_graphfile, "fl=%s\nfn=%s\n",
		    pmcstat_string_unintern(ct->pct_ifl),
		    pmcstat_string_unintern(ct->pct_ifn));
	} else if (pmcstat_image_addr2line(ct->pct_image, faddr,
	    sourcefile, sizeof(sourcefile), &fline,
	    funcname, sizeof(funcname))) {
		fprintf(args.pa_graphfile, "fl=%s\nfn=%s\n",
		    sourcefile, funcname);
	} else {
		if (ct->pct_sym != NULL)
			fprintf(args.pa_graphfile, "fl=???\nfn=%s\n",
			    pmcstat_string_unintern(ct->pct_sym->ps_name));
		else
			fprintf(args.pa_graphfile, "fl=???\nfn=%p\n",
			    (void *)(ct->pct_image->pi_vaddr + ct->pct_func));
	}

	/*
	 * Self cost.
	 */
	if (ct->pct_ninstr > 0) {
		/*
		 * Per location cost.
		 */
		for (i = 0; i < ct->pct_ninstr; i++) {
			addr = ct->pct_image->pi_vaddr +
			    ct->pct_instr[i].pctf_func;
			line = 0;
			pmcstat_image_addr2line(ct->pct_image, addr,
			    sourcefile, sizeof(sourcefile), &line,
			    funcname, sizeof(funcname));
			fprintf(args.pa_graphfile, "%p %u",
			    (void *)addr, line);
			for (j = 0; j<pmcstat_npmcs; j++)
				fprintf(args.pa_graphfile, " %u",
				    PMCPL_CT_SAMPLE(j,
				    &ct->pct_instr[i].pctf_samples));
			fprintf(args.pa_graphfile, "\n");
		}
	} else {
		/* Global cost function cost. */
		fprintf(args.pa_graphfile, "%p %u", (void *)faddr, fline);
		for (i = 0; i<pmcstat_npmcs ; i++)
			fprintf(args.pa_graphfile, " %u",
			    PMCPL_CT_SAMPLE(i, &ct->pct_samples));
		fprintf(args.pa_graphfile, "\n");
	}

	pmcpl_ct_node_printchild(ct, faddr, fline);
}

static void
pmcpl_ct_printnode(struct pmcpl_ct_node *ct)
{
	int i;

	if (ct == pmcpl_ct_root) {
		fprintf(args.pa_graphfile, "fn=root\n");
		fprintf(args.pa_graphfile, "0x0 1");
		for (i = 0; i<pmcstat_npmcs ; i++)
			fprintf(args.pa_graphfile, " 0");
		fprintf(args.pa_graphfile, "\n");
		pmcpl_ct_node_printchild(ct, 0, 0);
	} else
		pmcpl_ct_node_printself(ct);
}

/*
 * Breadth first traversal.
 */

static void
pmcpl_ct_bfs(struct pmcpl_ct_node *ct)
{
	int i;
	struct pmcpl_ct_node_hash *pch, *pchc;
	struct pmcpl_ct_node *child;
	STAILQ_HEAD(,pmcpl_ct_node_hash) q;

	STAILQ_INIT(&q);
	if ((pch = malloc(sizeof(*pch))) == NULL)
		err(EX_OSERR, "ERROR: Cannot allocate queue");
	pch->pch_ctnode = ct;
	STAILQ_INSERT_TAIL(&q, pch, pch_next);
	ct->pct_color = PMCPL_PCT_BLACK;

	while (!STAILQ_EMPTY(&q)) {
		pch = STAILQ_FIRST(&q);
		STAILQ_REMOVE_HEAD(&q, pch_next);
		pmcpl_ct_printnode(pch->pch_ctnode);
		for (i = 0; i<pch->pch_ctnode->pct_narc; i++) {
			child = pch->pch_ctnode->pct_arc[i].pcta_child;
			if (child->pct_color == PMCPL_PCT_WHITE) {
				child->pct_color = PMCPL_PCT_BLACK;
				if ((pchc = malloc(sizeof(*pchc))) == NULL)
					err(EX_OSERR,
					    "ERROR: Cannot allocate queue");
				pchc->pch_ctnode = child;
				STAILQ_INSERT_TAIL(&q, pchc, pch_next);
			}
		}
		free(pch);
	}
}

/*
 * Detect and fix inlined location.
 */

static void
_pmcpl_ct_expand_inline(struct pmcpl_ct_node *ct)
{
	int i, j;
	unsigned fline, line, v;
	uintfptr_t faddr, addr, pc;
	char sourcefile[PATH_MAX];
	char ffuncname[PATH_MAX], funcname[PATH_MAX];
	char buffer[PATH_MAX];
	struct pmcpl_ct_node *child;

	/*
	 * Resolve parent and compare to each instr location.
	 */
	faddr = ct->pct_image->pi_vaddr + ct->pct_func;
	fline = 0;
	if (!pmcstat_image_addr2line(ct->pct_image, faddr,
	    sourcefile, sizeof(sourcefile), &fline,
	    ffuncname, sizeof(ffuncname)))
		return;

	for (i = 0; i < ct->pct_ninstr; i++) {
		addr = ct->pct_image->pi_vaddr +
		    ct->pct_instr[i].pctf_func;
		line = 0;
		if (!pmcstat_image_addr2line(ct->pct_image, addr,
		    sourcefile, sizeof(sourcefile), &line,
		    funcname, sizeof(funcname)))
			continue;

		if (strcmp(funcname, ffuncname) == 0)
			continue;

		/*
		 * - Lookup/create inline node by function name.
		 * - Move instr PMCs to the inline node.
		 * - Link nodes.
		 * The lookup create a specific node per image/pc.
		 */
		if (args.pa_verbosity >= 2)
			fprintf(args.pa_printfile,
			    "WARNING: inlined function at %p %s in %s\n",
			    (void *)addr, funcname, ffuncname);

		snprintf(buffer, sizeof(buffer), "%s@%s",
			funcname, ffuncname);
		child = pmcpl_ct_node_hash_lookup(ct->pct_image,
		    ct->pct_func, ct->pct_sym, sourcefile, buffer);
		assert(child != NULL);
		pc = ct->pct_instr[i].pctf_func;
		for (j = 0; j<pmcstat_npmcs; j++) {
			v = PMCPL_CT_SAMPLE(j,
			    &ct->pct_instr[i].pctf_samples);
			if (v == 0)
				continue;
			pmcpl_ct_instr_add(child, j, pc, v);
			pmcpl_ct_node_update(ct, child, j, v, 0);
			if (j < ct->pct_samples.npmcs)
				ct->pct_samples.sb[j] -=
				    ct->pct_instr[i].pctf_samples.sb[j];
			ct->pct_instr[i].pctf_samples.sb[j] = 0;
		}
	}
}

static void
pmcpl_ct_expand_inline(void)
{
	int i;
	struct pmcpl_ct_node_hash *pch;

	if (!args.pa_ctdumpinstr)
		return;

	for (i = 0; i < PMCSTAT_NHASH; i++)
		STAILQ_FOREACH(pch, &pmcpl_ct_node_hash[i], pch_next)
			if (pch->pch_ctnode->pct_type == PMCPL_PCT_ADDR)
				_pmcpl_ct_expand_inline(pch->pch_ctnode);
}

/*
 * Clean the PMC name for Kcachegrind formula
 */

static void
pmcpl_ct_fixup_pmcname(char *s)
{
	char *p;

	for (p = s; *p; p++)
		if (!isalnum(*p))
			*p = '_';
}

/*
 * Print a calltree (KCachegrind) for all PMCs.
 */

static void
pmcpl_ct_print(void)
{
	int i;
	char name[40];
	struct pmcpl_ct_sample rsamples;

	pmcpl_ct_samples_root(&rsamples);
	pmcpl_ct_expand_inline();

	fprintf(args.pa_graphfile,
		"version: 1\n"
		"creator: pmcstat\n"
		"positions: instr line\n"
		"events:");
	for (i=0; i<pmcstat_npmcs; i++) {
		snprintf(name, sizeof(name), "%s_%d",
		    pmcstat_pmcindex_to_name(i), i);
		pmcpl_ct_fixup_pmcname(name);
		fprintf(args.pa_graphfile, " %s", name);
	}
	fprintf(args.pa_graphfile, "\nsummary:");
	for (i=0; i<pmcstat_npmcs ; i++)
		fprintf(args.pa_graphfile, " %u",
		    PMCPL_CT_SAMPLE(i, &rsamples));
	fprintf(args.pa_graphfile, "\n");
	pmcpl_ct_bfs(pmcpl_ct_root);
	pmcpl_ct_samples_free(&rsamples);
}

int
pmcpl_ct_configure(char *opt)
{

	if (strncmp(opt, "skiplink=", 9) == 0) {
		pmcstat_skiplink = atoi(opt+9);
	} else
		return (0);

	return (1);
}

int
pmcpl_ct_init(void)
{
	int i;

	pmcpl_ct_root = pmcpl_ct_node_allocate();

	for (i = 0; i < PMCSTAT_NHASH; i++)
		STAILQ_INIT(&pmcpl_ct_node_hash[i]);

	pmcpl_ct_samples_init(&pmcpl_ct_callid);

	return (0);
}

void
pmcpl_ct_shutdown(FILE *mf)
{
	int i;
	struct pmcpl_ct_node_hash *pch, *pchtmp;

	(void) mf;

	if (args.pa_flags & FLAG_DO_CALLGRAPHS)
		pmcpl_ct_print();

	/*
	 * Free memory.
	 */

	for (i = 0; i < PMCSTAT_NHASH; i++) {
		STAILQ_FOREACH_SAFE(pch, &pmcpl_ct_node_hash[i], pch_next,
		    pchtmp) {
			pmcpl_ct_node_free(pch->pch_ctnode);
			free(pch);
		}
	}

	pmcpl_ct_node_free(pmcpl_ct_root);
	pmcpl_ct_root = NULL;

	pmcpl_ct_samples_free(&pmcpl_ct_callid);
}

