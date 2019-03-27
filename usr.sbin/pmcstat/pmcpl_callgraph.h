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
 *
 * $FreeBSD$
 */

#ifndef	_PMCSTAT_PL_CALLGRAPH_H_
#define	_PMCSTAT_PL_CALLGRAPH_H_

/*
 * Each call graph node is tracked by a pmcstat_cgnode struct.
 */

struct pmcstat_cgnode {
	struct pmcstat_image	*pcg_image;
	uintfptr_t		pcg_func;
	uint32_t		pcg_count;
	uint32_t		pcg_nchildren;
	LIST_ENTRY(pmcstat_cgnode) pcg_sibling;
	LIST_HEAD(,pmcstat_cgnode) pcg_children;
};

struct pmcstat_cgnode_hash {
	struct pmcstat_cgnode  *pch_cgnode;
	pmc_id_t		pch_pmcid;
	LIST_ENTRY(pmcstat_cgnode_hash) pch_next;
};
extern LIST_HEAD(pmcstat_cgnode_hash_list, pmcstat_cgnode_hash) pmcstat_cgnode_hash[PMCSTAT_NHASH];
extern int pmcstat_cgnode_hash_count;

/* Function prototypes */
int pmcpl_cg_init(void);
void pmcpl_cg_shutdown(FILE *mf);
void pmcpl_cg_process(
    struct pmcstat_process *pp, struct pmcstat_pmcrecord *pmcr,
    uint32_t nsamples, uintfptr_t *cc, int usermode, uint32_t cpu);
int pmcpl_cg_topkeypress(int c, void *w);
void pmcpl_cg_topdisplay(void);
void pmcpl_cg_configure(char *opt);

#endif	/* _PMCSTAT_PL_CALLGRAPH_H_ */
