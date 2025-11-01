/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Code for Data Access Monitoring
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>

struct folio *damon_get_folio(unsigned long pfn);

void damon_ptep_mkold(pte_t *pte, struct vm_area_struct *vma, unsigned long addr);
void damon_pmdp_mkold(pmd_t *pmd, struct vm_area_struct *vma, unsigned long addr);
void damon_folio_mkold(struct folio *folio);
bool damon_folio_young(struct folio *folio);

int damon_cold_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s);
int damon_hot_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s);

bool damos_folio_filter_match(struct damos_filter *filter, struct folio *folio);
unsigned long damon_migrate_pages(struct list_head *folio_list, int target_nid);

bool damos_ops_has_filter(struct damos *s);
