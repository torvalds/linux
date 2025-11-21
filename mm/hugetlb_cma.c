// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/compiler.h>
#include <linux/mm_inline.h>

#include <asm/page.h>
#include <asm/setup.h>

#include <linux/hugetlb.h>
#include "internal.h"
#include "hugetlb_cma.h"


static struct cma *hugetlb_cma[MAX_NUMNODES];
static unsigned long hugetlb_cma_size_in_node[MAX_NUMNODES] __initdata;
static bool hugetlb_cma_only;
static unsigned long hugetlb_cma_size __initdata;

void hugetlb_cma_free_folio(struct folio *folio)
{
	int nid = folio_nid(folio);

	WARN_ON_ONCE(!cma_free_folio(hugetlb_cma[nid], folio));
}


struct folio *hugetlb_cma_alloc_folio(int order, gfp_t gfp_mask,
				      int nid, nodemask_t *nodemask)
{
	int node;
	struct folio *folio = NULL;

	if (hugetlb_cma[nid])
		folio = cma_alloc_folio(hugetlb_cma[nid], order, gfp_mask);

	if (!folio && !(gfp_mask & __GFP_THISNODE)) {
		for_each_node_mask(node, *nodemask) {
			if (node == nid || !hugetlb_cma[node])
				continue;

			folio = cma_alloc_folio(hugetlb_cma[node], order, gfp_mask);
			if (folio)
				break;
		}
	}

	if (folio)
		folio_set_hugetlb_cma(folio);

	return folio;
}

struct huge_bootmem_page * __init
hugetlb_cma_alloc_bootmem(struct hstate *h, int *nid, bool node_exact)
{
	struct cma *cma;
	struct huge_bootmem_page *m;
	int node = *nid;

	cma = hugetlb_cma[*nid];
	m = cma_reserve_early(cma, huge_page_size(h));
	if (!m) {
		if (node_exact)
			return NULL;

		for_each_node_mask(node, hugetlb_bootmem_nodes) {
			cma = hugetlb_cma[node];
			if (!cma || node == *nid)
				continue;
			m = cma_reserve_early(cma, huge_page_size(h));
			if (m) {
				*nid = node;
				break;
			}
		}
	}

	if (m) {
		m->flags = HUGE_BOOTMEM_CMA;
		m->cma = cma;
	}

	return m;
}


static bool cma_reserve_called __initdata;

static int __init cmdline_parse_hugetlb_cma(char *p)
{
	int nid, count = 0;
	unsigned long tmp;
	char *s = p;

	while (*s) {
		if (sscanf(s, "%lu%n", &tmp, &count) != 1)
			break;

		if (s[count] == ':') {
			if (tmp >= MAX_NUMNODES)
				break;
			nid = array_index_nospec(tmp, MAX_NUMNODES);

			s += count + 1;
			tmp = memparse(s, &s);
			hugetlb_cma_size_in_node[nid] = tmp;
			hugetlb_cma_size += tmp;

			/*
			 * Skip the separator if have one, otherwise
			 * break the parsing.
			 */
			if (*s == ',')
				s++;
			else
				break;
		} else {
			hugetlb_cma_size = memparse(p, &p);
			break;
		}
	}

	return 0;
}

early_param("hugetlb_cma", cmdline_parse_hugetlb_cma);

static int __init cmdline_parse_hugetlb_cma_only(char *p)
{
	return kstrtobool(p, &hugetlb_cma_only);
}

early_param("hugetlb_cma_only", cmdline_parse_hugetlb_cma_only);

void __init hugetlb_cma_reserve(int order)
{
	unsigned long size, reserved, per_node;
	bool node_specific_cma_alloc = false;
	int nid;

	/*
	 * HugeTLB CMA reservation is required for gigantic
	 * huge pages which could not be allocated via the
	 * page allocator. Just warn if there is any change
	 * breaking this assumption.
	 */
	VM_WARN_ON(order <= MAX_PAGE_ORDER);
	cma_reserve_called = true;

	if (!hugetlb_cma_size)
		return;

	hugetlb_bootmem_set_nodes();

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		if (hugetlb_cma_size_in_node[nid] == 0)
			continue;

		if (!node_isset(nid, hugetlb_bootmem_nodes)) {
			pr_warn("hugetlb_cma: invalid node %d specified\n", nid);
			hugetlb_cma_size -= hugetlb_cma_size_in_node[nid];
			hugetlb_cma_size_in_node[nid] = 0;
			continue;
		}

		if (hugetlb_cma_size_in_node[nid] < (PAGE_SIZE << order)) {
			pr_warn("hugetlb_cma: cma area of node %d should be at least %lu MiB\n",
				nid, (PAGE_SIZE << order) / SZ_1M);
			hugetlb_cma_size -= hugetlb_cma_size_in_node[nid];
			hugetlb_cma_size_in_node[nid] = 0;
		} else {
			node_specific_cma_alloc = true;
		}
	}

	/* Validate the CMA size again in case some invalid nodes specified. */
	if (!hugetlb_cma_size)
		return;

	if (hugetlb_cma_size < (PAGE_SIZE << order)) {
		pr_warn("hugetlb_cma: cma area should be at least %lu MiB\n",
			(PAGE_SIZE << order) / SZ_1M);
		hugetlb_cma_size = 0;
		return;
	}

	if (!node_specific_cma_alloc) {
		/*
		 * If 3 GB area is requested on a machine with 4 numa nodes,
		 * let's allocate 1 GB on first three nodes and ignore the last one.
		 */
		per_node = DIV_ROUND_UP(hugetlb_cma_size,
					nodes_weight(hugetlb_bootmem_nodes));
		pr_info("hugetlb_cma: reserve %lu MiB, up to %lu MiB per node\n",
			hugetlb_cma_size / SZ_1M, per_node / SZ_1M);
	}

	reserved = 0;
	for_each_node_mask(nid, hugetlb_bootmem_nodes) {
		int res;
		char name[CMA_MAX_NAME];

		if (node_specific_cma_alloc) {
			if (hugetlb_cma_size_in_node[nid] == 0)
				continue;

			size = hugetlb_cma_size_in_node[nid];
		} else {
			size = min(per_node, hugetlb_cma_size - reserved);
		}

		size = round_up(size, PAGE_SIZE << order);

		snprintf(name, sizeof(name), "hugetlb%d", nid);
		/*
		 * Note that 'order per bit' is based on smallest size that
		 * may be returned to CMA allocator in the case of
		 * huge page demotion.
		 */
		res = cma_declare_contiguous_multi(size, PAGE_SIZE << order,
					HUGETLB_PAGE_ORDER, name,
					&hugetlb_cma[nid], nid);
		if (res) {
			pr_warn("hugetlb_cma: reservation failed: err %d, node %d",
				res, nid);
			continue;
		}

		reserved += size;
		pr_info("hugetlb_cma: reserved %lu MiB on node %d\n",
			size / SZ_1M, nid);

		if (reserved >= hugetlb_cma_size)
			break;
	}

	if (!reserved)
		/*
		 * hugetlb_cma_size is used to determine if allocations from
		 * cma are possible.  Set to zero if no cma regions are set up.
		 */
		hugetlb_cma_size = 0;
}

void __init hugetlb_cma_check(void)
{
	if (!hugetlb_cma_size || cma_reserve_called)
		return;

	pr_warn("hugetlb_cma: the option isn't supported by current arch\n");
}

bool hugetlb_cma_exclusive_alloc(void)
{
	return hugetlb_cma_only;
}

unsigned long __init hugetlb_cma_total_size(void)
{
	return hugetlb_cma_size;
}

void __init hugetlb_cma_validate_params(void)
{
	if (!hugetlb_cma_size)
		hugetlb_cma_only = false;
}

bool __init hugetlb_early_cma(struct hstate *h)
{
	if (arch_has_huge_bootmem_alloc())
		return false;

	return hstate_is_gigantic(h) && hugetlb_cma_only;
}
