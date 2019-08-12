// SPDX-License-Identifier: GPL-2.0
#include "../util/util.h"
#include "../util/string2.h"
#include "../util/config.h"
#include "../perf.h"
#include "libslang.h"
#include "ui.h"
#include "util.h"
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/string.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>
#include "browser.h"
#include "helpline.h"
#include "keysyms.h"
#include "../util/color.h"
#include <linux/ctype.h>
#include <linux/zalloc.h>

static int ui_browser__percent_color(struct ui_browser *browser,
				     double percent, bool current)
{
	if (current && (!browser->use_navkeypressed || browser->navkeypressed))
		return HE_COLORSET_SELECTED;
	if (percent >= MIN_RED)
		return HE_COLORSET_TOP;
	if (percent >= MIN_GREEN)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

int ui_browser__set_color(struct ui_browser *browser, int color)
{
	int ret = browser->current_color;
	browser->current_color = color;
	SLsmg_set_color(color);
	return ret;
}

void ui_browser__set_percent_color(struct ui_browser *browser,
				   double percent, bool current)
{
	 int color = ui_browser__percent_color(browser, percent, current);
	 ui_browser__set_color(browser, color);
}

void ui_browser__gotorc_title(struct ui_browser *browser, int y, int x)
{
	SLsmg_gotorc(browser->y + y, browser->x + x);
}

void ui_browser__gotorc(struct ui_browser *browser, int y, int x)
{
	SLsmg_gotorc(browser->y + y + browser->extra_title_lines, browser->x + x);
}

void ui_browser__write_nstring(struct ui_browser *browser __maybe_unused, const char *msg,
			       unsigned int width)
{
	slsmg_write_nstring(msg, width);
}

void ui_browser__vprintf(struct ui_browser *browser __maybe_unused, const char *fmt, va_list args)
{
	slsmg_vprintf(fmt, args);
}

void ui_browser__printf(struct ui_browser *browser __maybe_unused, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ui_browser__vprintf(browser, fmt, args);
	va_end(args);
}

static struct list_head *
ui_browser__list_head_filter_entries(struct ui_browser *browser,
				     struct list_head *pos)
{
	do {
		if (!browser->filter || !browser->filter(browser, pos))
			return pos;
		pos = pos->next;
	} while (pos != browser->entries);

	return NULL;
}

static struct list_head *
ui_browser__list_head_filter_prev_entries(struct ui_browser *browser,
					  struct list_head *pos)
{
	do {
		if (!browser->filter || !browser->filter(browser, pos))
			return pos;
		pos = pos->prev;
	} while (pos != browser->entries);

	return NULL;
}

void ui_browser__list_head_seek(struct ui_browser *browser, off_t offset, int whence)
{
	struct list_head *head = browser->entries;
	struct list_head *pos;

	if (browser->nr_entries == 0)
		return;

	switch (whence) {
	case SEEK_SET:
		pos = ui_browser__list_head_filter_entries(browser, head->next);
		break;
	case SEEK_CUR:
		pos = browser->top;
		break;
	case SEEK_END:
		pos = ui_browser__list_head_filter_prev_entries(browser, head->prev);
		break;
	default:
		return;
	}

	assert(pos != NULL);

	if (offset > 0) {
		while (offset-- != 0)
			pos = ui_browser__list_head_filter_entries(browser, pos->next);
	} else {
		while (offset++ != 0)
			pos = ui_browser__list_head_filter_prev_entries(browser, pos->prev);
	}

	browser->top = pos;
}

void ui_browser__rb_tree_seek(struct ui_browser *browser, off_t offset, int whence)
{
	struct rb_root *root = browser->entries;
	struct rb_node *nd;

	switch (whence) {
	case SEEK_SET:
		nd = rb_first(root);
		break;
	case SEEK_CUR:
		nd = browser->top;
		break;
	case SEEK_END:
		nd = rb_last(root);
		break;
	default:
		return;
	}

	if (offset > 0) {
		while (offset-- != 0)
			nd = rb_next(nd);
	} else {
		while (offset++ != 0)
			nd = rb_prev(nd);
	}

	browser->top = nd;
}

unsigned int ui_browser__rb_tree_refresh(struct ui_browser *browser)
{
	struct rb_node *nd;
	int row = 0;

	if (browser->top == NULL)
                browser->top = rb_first(browser->entries);

	nd = browser->top;

	while (nd != NULL) {
		ui_browser__gotorc(browser, row, 0);
		browser->write(browser, nd, row);
		if (++row == browser->rows)
			break;
		nd = rb_next(nd);
	}

	return row;
}

bool ui_browser__is_current_entry(struct ui_browser *browser, unsigned row)
{
	return browser->top_idx + row == browser->index;
}

void ui_browser__refresh_dimensions(struct ui_browser *browser)
{
	browser->width = SLtt_Screen_Cols - 1;
	browser->height = browser->rows = SLtt_Screen_Rows - 2;
	browser->rows -= browser->extra_title_lines;
	browser->y = 1;
	browser->x = 0;
}

void ui_browser__handle_resize(struct ui_browser *browser)
{
	ui__refresh_dimensions(false);
	ui_browser__show(browser, browser->title, ui_helpline__current);
	ui_browser__refresh(browser);
}

int ui_browser__warning(struct ui_browser *browser, int timeout,
			const char *format, ...)
{
	va_list args;
	char *text;
	int key = 0, err;

	va_start(args, format);
	err = vasprintf(&text, format, args);
	va_end(args);

	if (err < 0) {
		va_start(args, format);
		ui_helpline__vpush(format, args);
		va_end(args);
	} else {
		while ((key = ui__question_window("Warning!", text,
						   "Press any key...",
						   timeout)) == K_RESIZE)
			ui_browser__handle_resize(browser);
		free(text);
	}

	return key;
}

int ui_browser__help_window(struct ui_browser *browser, const char *text)
{
	int key;

	while ((key = ui__help_window(text)) == K_RESIZE)
		ui_browser__handle_resize(browser);

	return key;
}

bool ui_browser__dialog_yesno(struct ui_browser *browser, const char *text)
{
	int key;

	while ((key = ui__dialog_yesno(text)) == K_RESIZE)
		ui_browser__handle_resize(browser);

	return key == K_ENTER || toupper(key) == 'Y';
}

void ui_browser__reset_index(struct ui_browser *browser)
{
	browser->index = browser->top_idx = 0;
	browser->seek(browser, 0, SEEK_SET);
}

void __ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	SLsmg_gotorc(0, 0);
	ui_browser__set_color(browser, HE_COLORSET_ROOT);
	ui_browser__write_nstring(browser, title, browser->width + 1);
}

void ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(browser, title);
	pthread_mutex_unlock(&ui__lock);
}

int ui_browser__show(struct ui_browser *browser, const char *title,
		     const char *helpline, ...)
{
	int err;
	va_list ap;

	if (browser->refresh_dimensions == NULL)
		browser->refresh_dimensions = ui_browser__refresh_dimensions;

	browser->refresh_dimensions(browser);

	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(browser, title);

	browser->title = title;
	zfree(&browser->helpline);

	va_start(ap, helpline);
	err = vasprintf(&browser->helpline, helpline, ap);
	va_end(ap);
	if (err > 0)
		ui_helpline__push(browser->helpline);
	pthread_mutex_unlock(&ui__lock);
	return err ? 0 : -1;
}

void ui_browser__hide(struct ui_browser *browser)
{
	pthread_mutex_lock(&ui__lock);
	ui_helpline__pop();
	zfree(&browser->helpline);
	pthread_mutex_unlock(&ui__lock);
}

static void ui_browser__scrollbar_set(struct ui_browser *browser)
{
	int height = browser->height, h = 0, pct = 0,
	    col = browser->width,
	    row = 0;

	if (browser->nr_entries > 1) {
		pct = ((browser->index * (browser->height - 1)) /
		       (browser->nr_entries - 1));
	}

	SLsmg_set_char_set(1);

	while (h < height) {
	        ui_browser__gotorc(browser, row++, col);
		SLsmg_write_char(h == pct ? SLSMG_DIAMOND_CHAR : SLSMG_CKBRD_CHAR);
		++h;
	}

	SLsmg_set_char_set(0);
}

static int __ui_browser__refresh(struct ui_browser *browser)
{
	int row;
	int width = browser->width;

	row = browser->refresh(browser);
	ui_browser__set_color(browser, HE_COLORSET_NORMAL);

	if (!browser->use_navkeypressed || browser->navkeypressed)
		ui_browser__scrollbar_set(browser);
	else
		width += 1;

	SLsmg_fill_region(browser->y + row + browser->extra_title_lines, browser->x,
			  browser->rows - row, width, ' ');

	return 0;
}

int ui_browser__refresh(struct ui_browser *browser)
{
	pthread_mutex_lock(&ui__lock);
	__ui_browser__refresh(browser);
	pthread_mutex_unlock(&ui__lock);

	return 0;
}

/*
 * Here we're updating nr_entries _after_ we started browsing, i.e.  we have to
 * forget about any reference to any entry in the underlying data structure,
 * that is why we do a SEEK_SET. Think about 'perf top' in the hists browser
 * after an output_resort and hist decay.
 */
void ui_browser__update_nr_entries(struct ui_browser *browser, u32 nr_entries)
{
	off_t offset = nr_entries - browser->nr_entries;

	browser->nr_entries = nr_entries;

	if (offset < 0) {
		if (browser->top_idx < (u64)-offset)
			offset = -browser->top_idx;

		browser->index += offset;
		browser->top_idx += offset;
	}

	browser->top = NULL;
	browser->seek(browser, browser->top_idx, SEEK_SET);
}

int ui_browser__run(struct ui_browser *browser, int delay_secs)
{
	int err, key;

	while (1) {
		off_t offset;

		pthread_mutex_lock(&ui__lock);
		err = __ui_browser__refresh(browser);
		SLsmg_refresh();
		pthread_mutex_unlock(&ui__lock);
		if (err < 0)
			break;

		key = ui__getch(delay_secs);

		if (key == K_RESIZE) {
			ui__refresh_dimensions(false);
			browser->refresh_dimensions(browser);
			__ui_browser__show_title(browser, browser->title);
			ui_helpline__puts(browser->helpline);
			continue;
		}

		if (browser->use_navkeypressed && !browser->navkeypressed) {
			if (key == K_DOWN || key == K_UP ||
			    (browser->columns && (key == K_LEFT || key == K_RIGHT)) ||
			    key == K_PGDN || key == K_PGUP ||
			    key == K_HOME || key == K_END ||
			    key == ' ') {
				browser->navkeypressed = true;
				continue;
			} else
				return key;
		}

		switch (key) {
		case K_DOWN:
			if (browser->index == browser->nr_entries - 1)
				break;
			++browser->index;
			if (browser->index == browser->top_idx + browser->rows) {
				++browser->top_idx;
				browser->seek(browser, +1, SEEK_CUR);
			}
			break;
		case K_UP:
			if (browser->index == 0)
				break;
			--browser->index;
			if (browser->index < browser->top_idx) {
				--browser->top_idx;
				browser->seek(browser, -1, SEEK_CUR);
			}
			break;
		case K_RIGHT:
			if (!browser->columns)
				goto out;
			if (browser->horiz_scroll < browser->columns - 1)
				++browser->horiz_scroll;
			break;
		case K_LEFT:
			if (!browser->columns)
				goto out;
			if (browser->horiz_scroll != 0)
				--browser->horiz_scroll;
			break;
		case K_PGDN:
		case ' ':
			if (browser->top_idx + browser->rows > browser->nr_entries - 1)
				break;

			offset = browser->rows;
			if (browser->index + offset > browser->nr_entries - 1)
				offset = browser->nr_entries - 1 - browser->index;
			browser->index += offset;
			browser->top_idx += offset;
			browser->seek(browser, +offset, SEEK_CUR);
			break;
		case K_PGUP:
			if (browser->top_idx == 0)
				break;

			if (browser->top_idx < browser->rows)
				offset = browser->top_idx;
			else
				offset = browser->rows;

			browser->index -= offset;
			browser->top_idx -= offset;
			browser->seek(browser, -offset, SEEK_CUR);
			break;
		case K_HOME:
			ui_browser__reset_index(browser);
			break;
		case K_END:
			offset = browser->rows - 1;
			if (offset >= browser->nr_entries)
				offset = browser->nr_entries - 1;

			browser->index = browser->nr_entries - 1;
			browser->top_idx = browser->index - offset;
			browser->seek(browser, -offset, SEEK_END);
			break;
		default:
		out:
			return key;
		}
	}
	return -1;
}

unsigned int ui_browser__list_head_refresh(struct ui_browser *browser)
{
	struct list_head *pos;
	struct list_head *head = browser->entries;
	int row = 0;

	if (browser->top == NULL || browser->top == browser->entries)
                browser->top = ui_browser__list_head_filter_entries(browser, head->next);

	pos = browser->top;

	list_for_each_from(pos, head) {
		if (!browser->filter || !browser->filter(browser, pos)) {
			ui_browser__gotorc(browser, row, 0);
			browser->write(browser, pos, row);
			if (++row == browser->rows)
				break;
		}
	}

	return row;
}

static struct ui_browser_colorset {
	const char *name, *fg, *bg;
	int colorset;
} ui_browser__colorsets[] = {
	{
		.colorset = HE_COLORSET_TOP,
		.name	  = "top",
		.fg	  = "red",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_MEDIUM,
		.name	  = "medium",
		.fg	  = "green",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_NORMAL,
		.name	  = "normal",
		.fg	  = "default",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_SELECTED,
		.name	  = "selected",
		.fg	  = "black",
		.bg	  = "yellow",
	},
	{
		.colorset = HE_COLORSET_JUMP_ARROWS,
		.name	  = "jump_arrows",
		.fg	  = "blue",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_ADDR,
		.name	  = "addr",
		.fg	  = "magenta",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_ROOT,
		.name	  = "root",
		.fg	  = "white",
		.bg	  = "blue",
	},
	{
		.name = NULL,
	}
};


static int ui_browser__color_config(const char *var, const char *value,
				    void *data __maybe_unused)
{
	char *fg = NULL, *bg;
	int i;

	/* same dir for all commands */
	if (!strstarts(var, "colors.") != 0)
		return 0;

	for (i = 0; ui_browser__colorsets[i].name != NULL; ++i) {
		const char *name = var + 7;

		if (strcmp(ui_browser__colorsets[i].name, name) != 0)
			continue;

		fg = strdup(value);
		if (fg == NULL)
			break;

		bg = strchr(fg, ',');
		if (bg == NULL)
			break;

		*bg = '\0';
		bg = skip_spaces(bg + 1);
		ui_browser__colorsets[i].bg = bg;
		ui_browser__colorsets[i].fg = fg;
		return 0;
	}

	free(fg);
	return -1;
}

void ui_browser__argv_seek(struct ui_browser *browser, off_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET:
		browser->top = browser->entries;
		break;
	case SEEK_CUR:
		browser->top = (char **)browser->top + offset;
		break;
	case SEEK_END:
		browser->top = (char **)browser->entries + browser->nr_entries - 1 + offset;
		break;
	default:
		return;
	}
	assert((char **)browser->top < (char **)browser->entries + browser->nr_entries);
	assert((char **)browser->top >= (char **)browser->entries);
}

unsigned int ui_browser__argv_refresh(struct ui_browser *browser)
{
	unsigned int row = 0, idx = browser->top_idx;
	char **pos;

	if (browser->top == NULL)
		browser->top = browser->entries;

	pos = (char **)browser->top;
	while (idx < browser->nr_entries &&
	       row < (unsigned)SLtt_Screen_Rows - 1) {
		assert(pos < (char **)browser->entries + browser->nr_entries);
		if (!browser->filter || !browser->filter(browser, *pos)) {
			ui_browser__gotorc(browser, row, 0);
			browser->write(browser, pos, row);
			if (++row == browser->rows)
				break;
		}

		++idx;
		++pos;
	}

	return row;
}

void __ui_browser__vline(struct ui_browser *browser, unsigned int column,
			 u16 start, u16 end)
{
	SLsmg_set_char_set(1);
	ui_browser__gotorc(browser, start, column);
	SLsmg_draw_vline(end - start + 1);
	SLsmg_set_char_set(0);
}

void ui_browser__write_graph(struct ui_browser *browser __maybe_unused,
			     int graph)
{
	SLsmg_set_char_set(1);
	SLsmg_write_char(graph);
	SLsmg_set_char_set(0);
}

static void __ui_browser__line_arrow_up(struct ui_browser *browser,
					unsigned int column,
					u64 start, u64 end)
{
	unsigned int row, end_row;

	SLsmg_set_char_set(1);

	if (start < browser->top_idx + browser->rows) {
		row = start - browser->top_idx;
		ui_browser__gotorc(browser, row, column);
		SLsmg_write_char(SLSMG_LLCORN_CHAR);
		ui_browser__gotorc(browser, row, column + 1);
		SLsmg_draw_hline(2);

		if (row-- == 0)
			goto out;
	} else
		row = browser->rows - 1;

	if (end > browser->top_idx)
		end_row = end - browser->top_idx;
	else
		end_row = 0;

	ui_browser__gotorc(browser, end_row, column);
	SLsmg_draw_vline(row - end_row + 1);

	ui_browser__gotorc(browser, end_row, column);
	if (end >= browser->top_idx) {
		SLsmg_write_char(SLSMG_ULCORN_CHAR);
		ui_browser__gotorc(browser, end_row, column + 1);
		SLsmg_write_char(SLSMG_HLINE_CHAR);
		ui_browser__gotorc(browser, end_row, column + 2);
		SLsmg_write_char(SLSMG_RARROW_CHAR);
	}
out:
	SLsmg_set_char_set(0);
}

static void __ui_browser__line_arrow_down(struct ui_browser *browser,
					  unsigned int column,
					  u64 start, u64 end)
{
	unsigned int row, end_row;

	SLsmg_set_char_set(1);

	if (start >= browser->top_idx) {
		row = start - browser->top_idx;
		ui_browser__gotorc(browser, row, column);
		SLsmg_write_char(SLSMG_ULCORN_CHAR);
		ui_browser__gotorc(browser, row, column + 1);
		SLsmg_draw_hline(2);

		if (++row == 0)
			goto out;
	} else
		row = 0;

	if (end >= browser->top_idx + browser->rows)
		end_row = browser->rows - 1;
	else
		end_row = end - browser->top_idx;

	ui_browser__gotorc(browser, row, column);
	SLsmg_draw_vline(end_row - row + 1);

	ui_browser__gotorc(browser, end_row, column);
	if (end < browser->top_idx + browser->rows) {
		SLsmg_write_char(SLSMG_LLCORN_CHAR);
		ui_browser__gotorc(browser, end_row, column + 1);
		SLsmg_write_char(SLSMG_HLINE_CHAR);
		ui_browser__gotorc(browser, end_row, column + 2);
		SLsmg_write_char(SLSMG_RARROW_CHAR);
	}
out:
	SLsmg_set_char_set(0);
}

void __ui_browser__line_arrow(struct ui_browser *browser, unsigned int column,
			      u64 start, u64 end)
{
	if (start > end)
		__ui_browser__line_arrow_up(browser, column, start, end);
	else
		__ui_browser__line_arrow_down(browser, column, start, end);
}

void ui_browser__mark_fused(struct ui_browser *browser, unsigned int column,
			    unsigned int row, bool arrow_down)
{
	unsigned int end_row;

	if (row >= browser->top_idx)
		end_row = row - browser->top_idx;
	else
		return;

	SLsmg_set_char_set(1);

	if (arrow_down) {
		ui_browser__gotorc(browser, end_row, column - 1);
		SLsmg_write_char(SLSMG_ULCORN_CHAR);
		ui_browser__gotorc(browser, end_row, column);
		SLsmg_draw_hline(2);
		ui_browser__gotorc(browser, end_row + 1, column - 1);
		SLsmg_write_char(SLSMG_LTEE_CHAR);
	} else {
		ui_browser__gotorc(browser, end_row, column - 1);
		SLsmg_write_char(SLSMG_LTEE_CHAR);
		ui_browser__gotorc(browser, end_row, column);
		SLsmg_draw_hline(2);
	}

	SLsmg_set_char_set(0);
}

void ui_browser__init(void)
{
	int i = 0;

	perf_config(ui_browser__color_config, NULL);

	while (ui_browser__colorsets[i].name) {
		struct ui_browser_colorset *c = &ui_browser__colorsets[i++];
		sltt_set_color(c->colorset, c->name, c->fg, c->bg);
	}
}
