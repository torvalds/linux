#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <string.h>
#include "map.h"
#include "symbol.h"
#include "util.h"
#include "tests.h"
#include "debug.h"
#include "machine.h"

static int vmlinux_matches_kallsyms_filter(struct map *map __maybe_unused,
					   struct symbol *sym)
{
	bool *visited = symbol__priv(sym);
	*visited = true;
	return 0;
}

#define UM(x) kallsyms_map->unmap_ip(kallsyms_map, (x))

int test__vmlinux_matches_kallsyms(int subtest __maybe_unused)
{
	int err = -1;
	struct rb_node *nd;
	struct symbol *sym;
	struct map *kallsyms_map, *vmlinux_map, *map;
	struct machine kallsyms, vmlinux;
	enum map_type type = MAP__FUNCTION;
	struct maps *maps = &vmlinux.kmaps.maps[type];
	u64 mem_start, mem_end;

	/*
	 * Step 1:
	 *
	 * Init the machines that will hold kernel, modules obtained from
	 * both vmlinux + .ko files and from /proc/kallsyms split by modules.
	 */
	machine__init(&kallsyms, "", HOST_KERNEL_ID);
	machine__init(&vmlinux, "", HOST_KERNEL_ID);

	/*
	 * Step 2:
	 *
	 * Create the kernel maps for kallsyms and the DSO where we will then
	 * load /proc/kallsyms. Also create the modules maps from /proc/modules
	 * and find the .ko files that match them in /lib/modules/`uname -r`/.
	 */
	if (machine__create_kernel_maps(&kallsyms) < 0) {
		pr_debug("machine__create_kernel_maps ");
		goto out;
	}

	/*
	 * Step 3:
	 *
	 * Load and split /proc/kallsyms into multiple maps, one per module.
	 */
	if (machine__load_kallsyms(&kallsyms, "/proc/kallsyms", type, NULL) <= 0) {
		pr_debug("dso__load_kallsyms ");
		goto out;
	}

	/*
	 * Step 4:
	 *
	 * kallsyms will be internally on demand sorted by name so that we can
	 * find the reference relocation * symbol, i.e. the symbol we will use
	 * to see if the running kernel was relocated by checking if it has the
	 * same value in the vmlinux file we load.
	 */
	kallsyms_map = machine__kernel_map(&kallsyms);

	/*
	 * Step 5:
	 *
	 * Now repeat step 2, this time for the vmlinux file we'll auto-locate.
	 */
	if (machine__create_kernel_maps(&vmlinux) < 0) {
		pr_debug("machine__create_kernel_maps ");
		goto out;
	}

	vmlinux_map = machine__kernel_map(&vmlinux);

	/*
	 * Step 6:
	 *
	 * Locate a vmlinux file in the vmlinux path that has a buildid that
	 * matches the one of the running kernel.
	 *
	 * While doing that look if we find the ref reloc symbol, if we find it
	 * we'll have its ref_reloc_symbol.unrelocated_addr and then
	 * maps__reloc_vmlinux will notice and set proper ->[un]map_ip routines
	 * to fixup the symbols.
	 */
	if (machine__load_vmlinux_path(&vmlinux, type,
				       vmlinux_matches_kallsyms_filter) <= 0) {
		pr_debug("Couldn't find a vmlinux that matches the kernel running on this machine, skipping test\n");
		err = TEST_SKIP;
		goto out;
	}

	err = 0;
	/*
	 * Step 7:
	 *
	 * Now look at the symbols in the vmlinux DSO and check if we find all of them
	 * in the kallsyms dso. For the ones that are in both, check its names and
	 * end addresses too.
	 */
	for (nd = rb_first(&vmlinux_map->dso->symbols[type]); nd; nd = rb_next(nd)) {
		struct symbol *pair, *first_pair;
		bool backwards = true;

		sym  = rb_entry(nd, struct symbol, rb_node);

		if (sym->start == sym->end)
			continue;

		mem_start = vmlinux_map->unmap_ip(vmlinux_map, sym->start);
		mem_end = vmlinux_map->unmap_ip(vmlinux_map, sym->end);

		first_pair = machine__find_kernel_symbol(&kallsyms, type,
							 mem_start, NULL, NULL);
		pair = first_pair;

		if (pair && UM(pair->start) == mem_start) {
next_pair:
			if (strcmp(sym->name, pair->name) == 0) {
				/*
				 * kallsyms don't have the symbol end, so we
				 * set that by using the next symbol start - 1,
				 * in some cases we get this up to a page
				 * wrong, trace_kmalloc when I was developing
				 * this code was one such example, 2106 bytes
				 * off the real size. More than that and we
				 * _really_ have a problem.
				 */
				s64 skew = mem_end - UM(pair->end);
				if (llabs(skew) >= page_size)
					pr_debug("%#" PRIx64 ": diff end addr for %s v: %#" PRIx64 " k: %#" PRIx64 "\n",
						 mem_start, sym->name, mem_end,
						 UM(pair->end));

				/*
				 * Do not count this as a failure, because we
				 * could really find a case where it's not
				 * possible to get proper function end from
				 * kallsyms.
				 */
				continue;

			} else {
				struct rb_node *nnd;
detour:
				nnd = backwards ? rb_prev(&pair->rb_node) :
						  rb_next(&pair->rb_node);
				if (nnd) {
					struct symbol *next = rb_entry(nnd, struct symbol, rb_node);

					if (UM(next->start) == mem_start) {
						pair = next;
						goto next_pair;
					}
				}

				if (backwards) {
					backwards = false;
					pair = first_pair;
					goto detour;
				}

				pr_debug("%#" PRIx64 ": diff name v: %s k: %s\n",
					 mem_start, sym->name, pair->name);
			}
		} else
			pr_debug("%#" PRIx64 ": %s not on kallsyms\n",
				 mem_start, sym->name);

		err = -1;
	}

	if (!verbose)
		goto out;

	pr_info("Maps only in vmlinux:\n");

	for (map = maps__first(maps); map; map = map__next(map)) {
		struct map *
		/*
		 * If it is the kernel, kallsyms is always "[kernel.kallsyms]", while
		 * the kernel will have the path for the vmlinux file being used,
		 * so use the short name, less descriptive but the same ("[kernel]" in
		 * both cases.
		 */
		pair = map_groups__find_by_name(&kallsyms.kmaps, type,
						(map->dso->kernel ?
							map->dso->short_name :
							map->dso->name));
		if (pair)
			pair->priv = 1;
		else
			map__fprintf(map, stderr);
	}

	pr_info("Maps in vmlinux with a different name in kallsyms:\n");

	for (map = maps__first(maps); map; map = map__next(map)) {
		struct map *pair;

		mem_start = vmlinux_map->unmap_ip(vmlinux_map, map->start);
		mem_end = vmlinux_map->unmap_ip(vmlinux_map, map->end);

		pair = map_groups__find(&kallsyms.kmaps, type, mem_start);
		if (pair == NULL || pair->priv)
			continue;

		if (pair->start == mem_start) {
			pair->priv = 1;
			pr_info(" %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s in kallsyms as",
				map->start, map->end, map->pgoff, map->dso->name);
			if (mem_end != pair->end)
				pr_info(":\n*%" PRIx64 "-%" PRIx64 " %" PRIx64,
					pair->start, pair->end, pair->pgoff);
			pr_info(" %s\n", pair->dso->name);
			pair->priv = 1;
		}
	}

	pr_info("Maps only in kallsyms:\n");

	maps = &kallsyms.kmaps.maps[type];

	for (map = maps__first(maps); map; map = map__next(map)) {
		if (!map->priv)
			map__fprintf(map, stderr);
	}
out:
	machine__exit(&kallsyms);
	machine__exit(&vmlinux);
	return err;
}
