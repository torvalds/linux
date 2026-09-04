// SPDX-License-Identifier: GPL-2.0-only

#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/compiler.h>
#include <linux/cpuset.h>
#include <linux/mm_inline.h>

#include <asm/page.h>
#include <asm/setup.h>

#include <linux/hugetlb.h>
#include <linux/memblock.h>
#include <linux/math.h>
#include <linux/math64.h>
#include "internal.h"
#include "hugetlb_cma.h"


static struct cma *hugetlb_cma[MAX_NUMNODES] __ro_after_init;
static unsigned long hugetlb_cma_size_in_node[MAX_NUMNODES] __initdata;
static bool hugetlb_cma_only __ro_after_init;
static unsigned long hugetlb_cma_size __ro_after_init;

static unsigned int hugetlb_cma_percent __initdata;
static unsigned int hugetlb_cma_percent_in_node[MAX_NUMNODES] __initdata;

#ifdef CONFIG_NUMA
static phys_addr_t __init memblock_node_memory_size(int nid)
{
	struct memblock_region *reg;
	phys_addr_t size = 0;

	for_each_mem_region(reg) {
		if (reg->nid == nid)
			size += reg->size;
	}
	return size;
}
#else
static phys_addr_t __init memblock_node_memory_size(int nid)
{
	return memblock_phys_mem_size();
}
#endif

void hugetlb_cma_free_frozen_folio(struct folio *folio)
{
	WARN_ON_ONCE(!cma_release_frozen(hugetlb_cma[folio_nid(folio)],
					 &folio->page, folio_nr_pages(folio)));
}

struct folio *hugetlb_cma_alloc_frozen_folio(int order, gfp_t gfp_mask,
		int nid, nodemask_t *nodemask)
{
	int node;
	struct folio *folio;
	struct page *page = NULL;
	const nodemask_t *nmask;
	unsigned int cpuset_mems_cookie;

	if (!hugetlb_cma_size)
		return NULL;

retry_cpuset:
	if (!nodemask) {
		cpuset_mems_cookie = read_mems_allowed_begin();
		nmask = &cpuset_current_mems_allowed;
	} else {
		nmask = nodemask;
	}

	if (hugetlb_cma[nid] && node_isset(nid, *nmask))
		page = cma_alloc_frozen_compound(hugetlb_cma[nid], order);

	if (!page && !(gfp_mask & __GFP_THISNODE)) {
		for_each_node_mask(node, *nmask) {
			if (node == nid || !hugetlb_cma[node])
				continue;

			page = cma_alloc_frozen_compound(hugetlb_cma[node], order);
			if (page)
				break;
		}
	}

	if (!page) {
		if (!nodemask &&
		    unlikely(read_mems_allowed_retry(cpuset_mems_cookie)))
			goto retry_cpuset;
		return NULL;
	}

	folio = page_folio(page);
	folio_set_hugetlb_cma(folio);
	return folio;
}

void * __init hugetlb_cma_alloc_bootmem(struct hstate *h, int nid, bool node_exact)
{
	struct cma *cma;
	void *m;
	int node;

	cma = hugetlb_cma[nid];
	m = cma_reserve_early(cma, huge_page_size(h));
	if (m || node_exact)
		return m;

	for_each_node_mask(node, hugetlb_bootmem_nodes) {
		cma = hugetlb_cma[node];
		if (!cma || node == nid)
			continue;
		m = cma_reserve_early(cma, huge_page_size(h));
		if (m)
			return m;
	}

	return NULL;
}

static int __init cmdline_parse_hugetlb_cma(char *p)
{
	int nid, count = 0;
	unsigned long tmp;
	char *s = p;

	while (*s) {
		if (sscanf(s, "%lu%n", &tmp, &count) != 1)
			break;

		if (s[count] == ':') {
			char *next;

			if (tmp >= MAX_NUMNODES)
				break;
			nid = array_index_nospec(tmp, MAX_NUMNODES);

			hugetlb_cma_size = 0;
			hugetlb_cma_percent = 0;

			s += count + 1;
			tmp = memparse(s, &next);
			if (*next == '%') {
				if (tmp > 100) {
					pr_warn("hugetlb_cma: invalid percentage %lu for node %d\n",
						tmp, nid);
					break;
				}
				hugetlb_cma_percent_in_node[nid] = tmp;
				hugetlb_cma_size_in_node[nid] = 0;
				s = next + 1;
			} else {
				hugetlb_cma_size_in_node[nid] = tmp;
				hugetlb_cma_percent_in_node[nid] = 0;
				s = next;
			}

			/*
			 * Skip the separator if have one, otherwise
			 * break the parsing.
			 */
			if (*s == ',')
				s++;
			else
				break;
		} else {
			char *next;

			tmp = memparse(p, &next);
			if (*next == '%') {
				if (tmp > 100) {
					pr_warn("hugetlb_cma: invalid percentage %lu\n", tmp);
				} else {
					hugetlb_cma_percent = tmp;
					hugetlb_cma_size = 0;
					for (nid = 0; nid < MAX_NUMNODES; nid++) {
						hugetlb_cma_size_in_node[nid] = 0;
						hugetlb_cma_percent_in_node[nid] = 0;
					}
				}
			} else {
				hugetlb_cma_size = tmp;
				hugetlb_cma_percent = 0;
				for (nid = 0; nid < MAX_NUMNODES; nid++) {
					hugetlb_cma_size_in_node[nid] = 0;
					hugetlb_cma_percent_in_node[nid] = 0;
				}
			}
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

unsigned int __weak arch_hugetlb_cma_order(void)
{
	return 0;
}

void __init hugetlb_cma_reserve(void)
{
	unsigned long size, reserved, per_node, order, gigantic_page_size;
	bool node_specific_cma_alloc = false;
	bool has_node_specific_param = false;
	int nid;

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		if (hugetlb_cma_size_in_node[nid] || hugetlb_cma_percent_in_node[nid]) {
			has_node_specific_param = true;
			break;
		}
	}

	if (has_node_specific_param) {
		hugetlb_cma_size = 0;
		for (nid = 0; nid < MAX_NUMNODES; nid++) {
			if (hugetlb_cma_percent_in_node[nid]) {
				phys_addr_t node_gfp_mem = memblock_node_memory_size(nid);
				u64 s;

				s = mul_u64_u32_div((u64)node_gfp_mem,
						    hugetlb_cma_percent_in_node[nid],
						    100);

				hugetlb_cma_size_in_node[nid] = s;
			}
			hugetlb_cma_size += hugetlb_cma_size_in_node[nid];
		}
	} else if (hugetlb_cma_percent) {
		hugetlb_cma_size = mul_u64_u32_div((u64)memblock_phys_mem_size(),
						   hugetlb_cma_percent, 100);
	}

	if (!hugetlb_cma_size)
		return;

	order = arch_hugetlb_cma_order();
	if (!order) {
		pr_warn("hugetlb_cma: the option isn't supported by current arch\n");
		return;
	}

	/*
	 * HugeTLB CMA reservation is required for gigantic
	 * huge pages which could not be allocated via the
	 * page allocator. Just warn if there is any change
	 * breaking this assumption.
	 */
	VM_WARN_ON(order <= MAX_PAGE_ORDER);
	gigantic_page_size = PAGE_SIZE << order;

	if (hugetlb_cma_percent) {
		unsigned long orig_size = hugetlb_cma_size;

		hugetlb_cma_size = ALIGN_DOWN(hugetlb_cma_size, PAGE_SIZE << order);
		if (orig_size && !hugetlb_cma_size)
			pr_warn("hugetlb_cma: reservation size rounded down to 0 from %lu MiB (%u%%)\n",
				orig_size / SZ_1M, hugetlb_cma_percent);
	} else if (has_node_specific_param) {
		hugetlb_cma_size = 0;
		for (nid = 0; nid < MAX_NUMNODES; nid++) {
			if (hugetlb_cma_percent_in_node[nid]) {
				unsigned long orig_size = hugetlb_cma_size_in_node[nid];

				hugetlb_cma_size_in_node[nid] =
					ALIGN_DOWN(hugetlb_cma_size_in_node[nid],
						   PAGE_SIZE << order);
				if (orig_size && !hugetlb_cma_size_in_node[nid])
					pr_warn("hugetlb_cma: reservation size rounded down to 0 from %lu MiB (%u%%) on node %d\n",
						orig_size / SZ_1M,
						hugetlb_cma_percent_in_node[nid],
						nid);
			}
			hugetlb_cma_size += hugetlb_cma_size_in_node[nid];
		}
	}

	hugetlb_bootmem_set_nodes();

	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		size = hugetlb_cma_size_in_node[nid];
		if (size == 0)
			continue;

		if (!node_isset(nid, hugetlb_bootmem_nodes)) {
			pr_warn("hugetlb_cma: invalid node %d specified\n", nid);
		} else if (!IS_ALIGNED(size, gigantic_page_size)) {
			pr_warn("hugetlb_cma: cma area of node %d must be a multiple of %lu MiB\n",
				nid, gigantic_page_size / SZ_1M);
		} else {
			node_specific_cma_alloc = true;
			continue;
		}

		hugetlb_cma_size -= size;
		hugetlb_cma_size_in_node[nid] = 0;
	}

	/* Validate the CMA size again in case some invalid nodes specified. */
	if (!hugetlb_cma_size)
		return;

	if (!IS_ALIGNED(hugetlb_cma_size, gigantic_page_size)) {
		pr_warn("hugetlb_cma: cma area must be a multiple of %lu MiB\n",
			gigantic_page_size / SZ_1M);
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
		per_node = round_up(per_node, gigantic_page_size);
		if (hugetlb_cma_percent)
			pr_info("hugetlb_cma: reserve %lu MiB (%u%%), up to %lu MiB per node\n",
				hugetlb_cma_size / SZ_1M, hugetlb_cma_percent,
				per_node / SZ_1M);
		else
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

		snprintf(name, sizeof(name), "hugetlb%d", nid);
		/*
		 * Note that 'order per bit' is based on smallest size that
		 * may be returned to CMA allocator in the case of
		 * huge page demotion.
		 */
		res = cma_declare_contiguous_multi(size, gigantic_page_size,
					HUGETLB_PAGE_ORDER, name,
					&hugetlb_cma[nid], nid);
		if (res || !cma_validate_zones(hugetlb_cma[nid])) {
			pr_warn("hugetlb_cma: %s: err %d, node %d\n",
				res ? "reservation failed" : "reserved area spans zones",
				res, nid);
			hugetlb_cma[nid] = NULL;
			continue;
		}

		reserved += size;
		if (hugetlb_cma_percent_in_node[nid])
			pr_info("hugetlb_cma: reserved %lu MiB (%u%%) on node %d\n",
				size / SZ_1M, hugetlb_cma_percent_in_node[nid], nid);
		else
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
