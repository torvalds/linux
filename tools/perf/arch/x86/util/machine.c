// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/string.h>
#include <stdlib.h>

#include "../../util/machine.h"
#include "../../util/map.h"
#include "../../util/symbol.h"
#include "../../util/sane_ctype.h"

#include <symbol/kallsyms.h>

#if defined(__x86_64__)

struct extra_kernel_map_info {
	int cnt;
	int max_cnt;
	struct extra_kernel_map *maps;
	bool get_entry_trampolines;
	u64 entry_trampoline;
};

static int add_extra_kernel_map(struct extra_kernel_map_info *mi, u64 start,
				u64 end, u64 pgoff, const char *name)
{
	if (mi->cnt >= mi->max_cnt) {
		void *buf;
		size_t sz;

		mi->max_cnt = mi->max_cnt ? mi->max_cnt * 2 : 32;
		sz = sizeof(struct extra_kernel_map) * mi->max_cnt;
		buf = realloc(mi->maps, sz);
		if (!buf)
			return -1;
		mi->maps = buf;
	}

	mi->maps[mi->cnt].start = start;
	mi->maps[mi->cnt].end   = end;
	mi->maps[mi->cnt].pgoff = pgoff;
	strlcpy(mi->maps[mi->cnt].name, name, KMAP_NAME_LEN);

	mi->cnt += 1;

	return 0;
}

static int find_extra_kernel_maps(void *arg, const char *name, char type,
				  u64 start)
{
	struct extra_kernel_map_info *mi = arg;

	if (!mi->entry_trampoline && kallsyms2elf_binding(type) == STB_GLOBAL &&
	    !strcmp(name, "_entry_trampoline")) {
		mi->entry_trampoline = start;
		return 0;
	}

	if (is_entry_trampoline(name)) {
		u64 end = start + page_size;

		return add_extra_kernel_map(mi, start, end, 0, name);
	}

	return 0;
}

int machine__create_extra_kernel_maps(struct machine *machine,
				      struct dso *kernel)
{
	struct extra_kernel_map_info mi = { .cnt = 0, };
	char filename[PATH_MAX];
	int ret;
	int i;

	machine__get_kallsyms_filename(machine, filename, PATH_MAX);

	if (symbol__restricted_filename(filename, "/proc/kallsyms"))
		return 0;

	ret = kallsyms__parse(filename, &mi, find_extra_kernel_maps);
	if (ret)
		goto out_free;

	if (!mi.entry_trampoline)
		goto out_free;

	for (i = 0; i < mi.cnt; i++) {
		struct extra_kernel_map *xm = &mi.maps[i];

		xm->pgoff = mi.entry_trampoline;
		ret = machine__create_extra_kernel_map(machine, kernel, xm);
		if (ret)
			goto out_free;
	}

	machine->trampolines_mapped = mi.cnt;
out_free:
	free(mi.maps);
	return ret;
}

#endif
