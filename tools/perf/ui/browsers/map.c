// SPDX-License-Identifier: GPL-2.0
#include <elf.h>
#include <inttypes.h>
#include <sys/ttydefaults.h>
#include <string.h>
#include <linux/bitops.h>
#include "../../util/util.h"
#include "../../util/debug.h"
#include "../../util/map.h"
#include "../../util/symbol.h"
#include "../browser.h"
#include "../helpline.h"
#include "../keysyms.h"
#include "map.h"

#include "sane_ctype.h"

struct map_browser {
	struct ui_browser b;
	struct map	  *map;
	u8		  addrlen;
};

static void map_browser__write(struct ui_browser *browser, void *nd, int row)
{
	struct symbol *sym = rb_entry(nd, struct symbol, rb_node);
	struct map_browser *mb = container_of(browser, struct map_browser, b);
	bool current_entry = ui_browser__is_current_entry(browser, row);
	int width;

	ui_browser__set_percent_color(browser, 0, current_entry);
	ui_browser__printf(browser, "%*" PRIx64 " %*" PRIx64 " %c ",
			   mb->addrlen, sym->start, mb->addrlen, sym->end,
			   sym->binding == STB_GLOBAL ? 'g' :
				sym->binding == STB_LOCAL  ? 'l' : 'w');
	width = browser->width - ((mb->addrlen * 2) + 4);
	if (width > 0)
		ui_browser__write_nstring(browser, sym->name, width);
}

/* FIXME uber-kludgy, see comment on cmd_report... */
static u32 *symbol__browser_index(struct symbol *browser)
{
	return ((void *)browser) - sizeof(struct rb_node) - sizeof(u32);
}

static int map_browser__search(struct map_browser *browser)
{
	char target[512];
	struct symbol *sym;
	int err = ui_browser__input_window("Search by name/addr",
					   "Prefix with 0x to search by address",
					   target, "ENTER: OK, ESC: Cancel", 0);
	if (err != K_ENTER)
		return -1;

	if (target[0] == '0' && tolower(target[1]) == 'x') {
		u64 addr = strtoull(target, NULL, 16);
		sym = map__find_symbol(browser->map, addr);
	} else
		sym = map__find_symbol_by_name(browser->map, target);

	if (sym != NULL) {
		u32 *idx = symbol__browser_index(sym);

		browser->b.top = &sym->rb_node;
		browser->b.index = browser->b.top_idx = *idx;
	} else
		ui_helpline__fpush("%s not found!", target);

	return 0;
}

static int map_browser__run(struct map_browser *browser)
{
	int key;

	if (ui_browser__show(&browser->b, browser->map->dso->long_name,
			     "Press ESC to exit, %s / to search",
			     verbose > 0 ? "" : "restart with -v to use") < 0)
		return -1;

	while (1) {
		key = ui_browser__run(&browser->b, 0);

		switch (key) {
		case '/':
			if (verbose > 0)
				map_browser__search(browser);
		default:
			break;
                case K_LEFT:
                case K_ESC:
                case 'q':
                case CTRL('c'):
                        goto out;
		}
	}
out:
	ui_browser__hide(&browser->b);
	return key;
}

int map__browse(struct map *map)
{
	struct map_browser mb = {
		.b = {
			.entries = &map->dso->symbols,
			.refresh = ui_browser__rb_tree_refresh,
			.seek	 = ui_browser__rb_tree_seek,
			.write	 = map_browser__write,
		},
		.map = map,
	};
	struct rb_node *nd;
	char tmp[BITS_PER_LONG / 4];
	u64 maxaddr = 0;

	for (nd = rb_first(mb.b.entries); nd; nd = rb_next(nd)) {
		struct symbol *pos = rb_entry(nd, struct symbol, rb_node);

		if (maxaddr < pos->end)
			maxaddr = pos->end;
		if (verbose > 0) {
			u32 *idx = symbol__browser_index(pos);
			*idx = mb.b.nr_entries;
		}
		++mb.b.nr_entries;
	}

	mb.addrlen = snprintf(tmp, sizeof(tmp), "%" PRIx64, maxaddr);
	return map_browser__run(&mb);
}
