// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "dso.h"
#include "map.h"
#include "symbol.h"
#include <internal/lib.h> // page_size
#include "tests.h"
#include "debug.h"
#include "machine.h"

#define UM(x) map__unmap_ip(kallsyms_map, (x))

static bool is_ignored_symbol(const char *name, char type)
{
	/* Symbol names that exactly match to the following are ignored.*/
	static const char * const ignored_symbols[] = {
		/*
		 * Symbols which vary between passes. Passes 1 and 2 must have
		 * identical symbol lists. The kallsyms_* symbols below are
		 * only added after pass 1, they would be included in pass 2
		 * when --all-symbols is specified so exclude them to get a
		 * stable symbol list.
		 */
		"kallsyms_addresses",
		"kallsyms_offsets",
		"kallsyms_relative_base",
		"kallsyms_num_syms",
		"kallsyms_names",
		"kallsyms_markers",
		"kallsyms_token_table",
		"kallsyms_token_index",
		/* Exclude linker generated symbols which vary between passes */
		"_SDA_BASE_",		/* ppc */
		"_SDA2_BASE_",		/* ppc */
		NULL
	};

	/* Symbol names that begin with the following are ignored.*/
	static const char * const ignored_prefixes[] = {
		"$",			/* local symbols for ARM, MIPS, etc. */
		".L",			/* local labels, .LBB,.Ltmpxxx,.L__unnamed_xx,.LASANPC, etc. */
		"__crc_",		/* modversions */
		"__efistub_",		/* arm64 EFI stub namespace */
		"__kvm_nvhe_$",		/* arm64 local symbols in non-VHE KVM namespace */
		"__kvm_nvhe_.L",	/* arm64 local symbols in non-VHE KVM namespace */
		"__AArch64ADRPThunk_",	/* arm64 lld */
		"__ARMV5PILongThunk_",	/* arm lld */
		"__ARMV7PILongThunk_",
		"__ThumbV7PILongThunk_",
		"__LA25Thunk_",		/* mips lld */
		"__microLA25Thunk_",
		NULL
	};

	/* Symbol names that end with the following are ignored.*/
	static const char * const ignored_suffixes[] = {
		"_from_arm",		/* arm */
		"_from_thumb",		/* arm */
		"_veneer",		/* arm */
		NULL
	};

	/* Symbol names that contain the following are ignored.*/
	static const char * const ignored_matches[] = {
		".long_branch.",	/* ppc stub */
		".plt_branch.",		/* ppc stub */
		NULL
	};

	const char * const *p;

	for (p = ignored_symbols; *p; p++)
		if (!strcmp(name, *p))
			return true;

	for (p = ignored_prefixes; *p; p++)
		if (!strncmp(name, *p, strlen(*p)))
			return true;

	for (p = ignored_suffixes; *p; p++) {
		int l = strlen(name) - strlen(*p);

		if (l >= 0 && !strcmp(name + l, *p))
			return true;
	}

	for (p = ignored_matches; *p; p++) {
		if (strstr(name, *p))
			return true;
	}

	if (type == 'U' || type == 'u')
		return true;
	/* exclude debugging symbols */
	if (type == 'N' || type == 'n')
		return true;

	if (toupper(type) == 'A') {
		/* Keep these useful absolute symbols */
		if (strcmp(name, "__kernel_syscall_via_break") &&
		    strcmp(name, "__kernel_syscall_via_epc") &&
		    strcmp(name, "__kernel_sigtramp") &&
		    strcmp(name, "__gp"))
			return true;
	}

	return false;
}

struct test__vmlinux_matches_kallsyms_cb_args {
	struct machine kallsyms;
	struct map *vmlinux_map;
	bool header_printed;
};

static int test__vmlinux_matches_kallsyms_cb1(struct map *map, void *data)
{
	struct test__vmlinux_matches_kallsyms_cb_args *args = data;
	struct dso *dso = map__dso(map);
	/*
	 * If it is the kernel, kallsyms is always "[kernel.kallsyms]", while
	 * the kernel will have the path for the vmlinux file being used, so use
	 * the short name, less descriptive but the same ("[kernel]" in both
	 * cases.
	 */
	struct map *pair = maps__find_by_name(args->kallsyms.kmaps,
					(dso->kernel ? dso->short_name : dso->name));

	if (pair)
		map__set_priv(pair, 1);
	else {
		if (!args->header_printed) {
			pr_info("WARN: Maps only in vmlinux:\n");
			args->header_printed = true;
		}
		map__fprintf(map, stderr);
	}
	return 0;
}

static int test__vmlinux_matches_kallsyms_cb2(struct map *map, void *data)
{
	struct test__vmlinux_matches_kallsyms_cb_args *args = data;
	struct map *pair;
	u64 mem_start = map__unmap_ip(args->vmlinux_map, map__start(map));
	u64 mem_end = map__unmap_ip(args->vmlinux_map, map__end(map));

	pair = maps__find(args->kallsyms.kmaps, mem_start);
	if (pair == NULL || map__priv(pair))
		return 0;

	if (map__start(pair) == mem_start) {
		struct dso *dso = map__dso(map);

		if (!args->header_printed) {
			pr_info("WARN: Maps in vmlinux with a different name in kallsyms:\n");
			args->header_printed = true;
		}

		pr_info("WARN: %" PRIx64 "-%" PRIx64 " %" PRIx64 " %s in kallsyms as",
			map__start(map), map__end(map), map__pgoff(map), dso->name);
		if (mem_end != map__end(pair))
			pr_info(":\nWARN: *%" PRIx64 "-%" PRIx64 " %" PRIx64,
				map__start(pair), map__end(pair), map__pgoff(pair));
		pr_info(" %s\n", dso->name);
		map__set_priv(pair, 1);
	}
	return 0;
}

static int test__vmlinux_matches_kallsyms_cb3(struct map *map, void *data)
{
	struct test__vmlinux_matches_kallsyms_cb_args *args = data;

	if (!map__priv(map)) {
		if (!args->header_printed) {
			pr_info("WARN: Maps only in kallsyms:\n");
			args->header_printed = true;
		}
		map__fprintf(map, stderr);
	}
	return 0;
}

static int test__vmlinux_matches_kallsyms(struct test_suite *test __maybe_unused,
					int subtest __maybe_unused)
{
	int err = TEST_FAIL;
	struct rb_node *nd;
	struct symbol *sym;
	struct map *kallsyms_map;
	struct machine vmlinux;
	struct maps *maps;
	u64 mem_start, mem_end;
	struct test__vmlinux_matches_kallsyms_cb_args args;

	/*
	 * Step 1:
	 *
	 * Init the machines that will hold kernel, modules obtained from
	 * both vmlinux + .ko files and from /proc/kallsyms split by modules.
	 */
	machine__init(&args.kallsyms, "", HOST_KERNEL_ID);
	machine__init(&vmlinux, "", HOST_KERNEL_ID);

	maps = machine__kernel_maps(&vmlinux);

	/*
	 * Step 2:
	 *
	 * Create the kernel maps for kallsyms and the DSO where we will then
	 * load /proc/kallsyms. Also create the modules maps from /proc/modules
	 * and find the .ko files that match them in /lib/modules/`uname -r`/.
	 */
	if (machine__create_kernel_maps(&args.kallsyms) < 0) {
		pr_debug("machine__create_kernel_maps failed");
		err = TEST_SKIP;
		goto out;
	}

	/*
	 * Step 3:
	 *
	 * Load and split /proc/kallsyms into multiple maps, one per module.
	 * Do not use kcore, as this test was designed before kcore support
	 * and has parts that only make sense if using the non-kcore code.
	 * XXX: extend it to stress the kcorre code as well, hint: the list
	 * of modules extracted from /proc/kcore, in its current form, can't
	 * be compacted against the list of modules found in the "vmlinux"
	 * code and with the one got from /proc/modules from the "kallsyms" code.
	 */
	if (machine__load_kallsyms(&args.kallsyms, "/proc/kallsyms") <= 0) {
		pr_debug("machine__load_kallsyms failed");
		err = TEST_SKIP;
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
	kallsyms_map = machine__kernel_map(&args.kallsyms);

	/*
	 * Step 5:
	 *
	 * Now repeat step 2, this time for the vmlinux file we'll auto-locate.
	 */
	if (machine__create_kernel_maps(&vmlinux) < 0) {
		pr_info("machine__create_kernel_maps failed");
		goto out;
	}

	args.vmlinux_map = machine__kernel_map(&vmlinux);

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
	if (machine__load_vmlinux_path(&vmlinux) <= 0) {
		pr_info("Couldn't find a vmlinux that matches the kernel running on this machine, skipping test\n");
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
	map__for_each_symbol(args.vmlinux_map, sym, nd) {
		struct symbol *pair, *first_pair;

		sym  = rb_entry(nd, struct symbol, rb_node);

		if (sym->start == sym->end)
			continue;

		mem_start = map__unmap_ip(args.vmlinux_map, sym->start);
		mem_end = map__unmap_ip(args.vmlinux_map, sym->end);

		first_pair = machine__find_kernel_symbol(&args.kallsyms, mem_start, NULL);
		pair = first_pair;

		if (pair && UM(pair->start) == mem_start) {
next_pair:
			if (arch__compare_symbol_names(sym->name, pair->name) == 0) {
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
					pr_debug("WARN: %#" PRIx64 ": diff end addr for %s v: %#" PRIx64 " k: %#" PRIx64 "\n",
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
				pair = machine__find_kernel_symbol_by_name(&args.kallsyms,
									   sym->name, NULL);
				if (pair) {
					if (UM(pair->start) == mem_start)
						goto next_pair;

					pr_debug("WARN: %#" PRIx64 ": diff name v: %s k: %s\n",
						 mem_start, sym->name, pair->name);
				} else {
					pr_debug("WARN: %#" PRIx64 ": diff name v: %s k: %s\n",
						 mem_start, sym->name, first_pair->name);
				}

				continue;
			}
		} else if (mem_start == map__end(args.kallsyms.vmlinux_map)) {
			/*
			 * Ignore aliases to _etext, i.e. to the end of the kernel text area,
			 * such as __indirect_thunk_end.
			 */
			continue;
		} else if (is_ignored_symbol(sym->name, sym->type)) {
			/*
			 * Ignore hidden symbols, see scripts/kallsyms.c for the details
			 */
			continue;
		} else {
			pr_debug("ERR : %#" PRIx64 ": %s not on kallsyms\n",
				 mem_start, sym->name);
		}

		err = -1;
	}

	if (verbose <= 0)
		goto out;

	args.header_printed = false;
	maps__for_each_map(maps, test__vmlinux_matches_kallsyms_cb1, &args);

	args.header_printed = false;
	maps__for_each_map(maps, test__vmlinux_matches_kallsyms_cb2, &args);

	args.header_printed = false;
	maps = machine__kernel_maps(&args.kallsyms);
	maps__for_each_map(maps, test__vmlinux_matches_kallsyms_cb3, &args);

out:
	machine__exit(&args.kallsyms);
	machine__exit(&vmlinux);
	return err;
}

DEFINE_SUITE("vmlinux symtab matches kallsyms", vmlinux_matches_kallsyms);
