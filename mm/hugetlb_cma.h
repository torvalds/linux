/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HUGETLB_CMA_H
#define _LINUX_HUGETLB_CMA_H

#ifdef CONFIG_CMA
void hugetlb_cma_free_folio(struct folio *folio);
struct folio *hugetlb_cma_alloc_folio(struct hstate *h, gfp_t gfp_mask,
				      int nid, nodemask_t *nodemask);
struct huge_bootmem_page *hugetlb_cma_alloc_bootmem(struct hstate *h, int *nid,
						    bool node_exact);
void hugetlb_cma_check(void);
bool hugetlb_cma_exclusive_alloc(void);
unsigned long hugetlb_cma_total_size(void);
void hugetlb_cma_validate_params(void);
bool hugetlb_early_cma(struct hstate *h);
#else
static inline void hugetlb_cma_free_folio(struct folio *folio)
{
}

static inline struct folio *hugetlb_cma_alloc_folio(struct hstate *h,
	    gfp_t gfp_mask, int nid, nodemask_t *nodemask)
{
	return NULL;
}

static inline
struct huge_bootmem_page *hugetlb_cma_alloc_bootmem(struct hstate *h, int *nid,
						    bool node_exact)
{
	return NULL;
}

static inline void hugetlb_cma_check(void)
{
}

static inline bool hugetlb_cma_exclusive_alloc(void)
{
	return false;
}

static inline unsigned long hugetlb_cma_total_size(void)
{
	return 0;
}

static inline void hugetlb_cma_validate_params(void)
{
}

static inline bool hugetlb_early_cma(struct hstate *h)
{
	return false;
}
#endif
#endif
