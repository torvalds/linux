#include "../util.h"
#include "../../perf.h"
#include "libslang.h"
#include <newt.h>
#include "ui.h"
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>
#include "browser.h"
#include "helpline.h"
#include "../color.h"

int newtGetKey(void);

static int ui_browser__percent_color(double percent, bool current)
{
	if (current)
		return HE_COLORSET_SELECTED;
	if (percent >= MIN_RED)
		return HE_COLORSET_TOP;
	if (percent >= MIN_GREEN)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

void ui_browser__set_color(struct ui_browser *self __used, int color)
{
	SLsmg_set_color(color);
}

void ui_browser__set_percent_color(struct ui_browser *self,
				   double percent, bool current)
{
	 int color = ui_browser__percent_color(percent, current);
	 ui_browser__set_color(self, color);
}

void ui_browser__gotorc(struct ui_browser *self, int y, int x)
{
	SLsmg_gotorc(self->y + y, self->x + x);
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

void ui_browser__list_head_seek(struct ui_browser *self, off_t offset, int whence)
{
	struct list_head *head = self->entries;
	struct list_head *pos;

	if (self->nr_entries == 0)
		return;

	switch (whence) {
	case SEEK_SET:
		pos = ui_browser__list_head_filter_entries(self, head->next);
		break;
	case SEEK_CUR:
		pos = self->top;
		break;
	case SEEK_END:
		pos = ui_browser__list_head_filter_prev_entries(self, head->prev);
		break;
	default:
		return;
	}

	assert(pos != NULL);

	if (offset > 0) {
		while (offset-- != 0)
			pos = ui_browser__list_head_filter_entries(self, pos->next);
	} else {
		while (offset++ != 0)
			pos = ui_browser__list_head_filter_prev_entries(self, pos->prev);
	}

	self->top = pos;
}

void ui_browser__rb_tree_seek(struct ui_browser *self, off_t offset, int whence)
{
	struct rb_root *root = self->entries;
	struct rb_node *nd;

	switch (whence) {
	case SEEK_SET:
		nd = rb_first(root);
		break;
	case SEEK_CUR:
		nd = self->top;
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

	self->top = nd;
}

unsigned int ui_browser__rb_tree_refresh(struct ui_browser *self)
{
	struct rb_node *nd;
	int row = 0;

	if (self->top == NULL)
                self->top = rb_first(self->entries);

	nd = self->top;

	while (nd != NULL) {
		ui_browser__gotorc(self, row, 0);
		self->write(self, nd, row);
		if (++row == self->height)
			break;
		nd = rb_next(nd);
	}

	return row;
}

bool ui_browser__is_current_entry(struct ui_browser *self, unsigned row)
{
	return self->top_idx + row == self->index;
}

void ui_browser__refresh_dimensions(struct ui_browser *self)
{
	self->width = SLtt_Screen_Cols - 1;
	self->height = SLtt_Screen_Rows - 2;
	self->y = 1;
	self->x = 0;
}

void ui_browser__reset_index(struct ui_browser *self)
{
	self->index = self->top_idx = 0;
	self->seek(self, 0, SEEK_SET);
}

void __ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	SLsmg_gotorc(0, 0);
	ui_browser__set_color(browser, NEWT_COLORSET_ROOT);
	slsmg_write_nstring(title, browser->width + 1);
}

void ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(browser, title);
	pthread_mutex_unlock(&ui__lock);
}

int ui_browser__show(struct ui_browser *self, const char *title,
		     const char *helpline, ...)
{
	int err;
	va_list ap;

	ui_browser__refresh_dimensions(self);

	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(self, title);

	self->title = title;
	free(self->helpline);
	self->helpline = NULL;

	va_start(ap, helpline);
	err = vasprintf(&self->helpline, helpline, ap);
	va_end(ap);
	if (err > 0)
		ui_helpline__push(self->helpline);
	pthread_mutex_unlock(&ui__lock);
	return err ? 0 : -1;
}

void ui_browser__hide(struct ui_browser *browser __used)
{
	pthread_mutex_lock(&ui__lock);
	ui_helpline__pop();
	pthread_mutex_unlock(&ui__lock);
}

static void ui_browser__scrollbar_set(struct ui_browser *browser)
{
	int height = browser->height, h = 0, pct = 0,
	    col = browser->width,
	    row = browser->y - 1;

	if (browser->nr_entries > 1) {
		pct = ((browser->index * (browser->height - 1)) /
		       (browser->nr_entries - 1));
	}

	while (h < height) {
	        ui_browser__gotorc(browser, row++, col);
		SLsmg_set_char_set(1);
		SLsmg_write_char(h == pct ? SLSMG_DIAMOND_CHAR : SLSMG_BOARD_CHAR);
		SLsmg_set_char_set(0);
		++h;
	}
}

static int __ui_browser__refresh(struct ui_browser *browser)
{
	int row;

	row = browser->refresh(browser);
	ui_browser__set_color(browser, HE_COLORSET_NORMAL);
	SLsmg_fill_region(browser->y + row, browser->x,
			  browser->height - row, browser->width, ' ');
	ui_browser__scrollbar_set(browser);

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

int ui_browser__run(struct ui_browser *self, int delay_secs)
{
	int err, key;
	struct timeval timeout, *ptimeout = delay_secs ? &timeout : NULL;

	pthread__unblock_sigwinch();

	while (1) {
		off_t offset;
		fd_set read_set;

		pthread_mutex_lock(&ui__lock);
		err = __ui_browser__refresh(self);
		SLsmg_refresh();
		pthread_mutex_unlock(&ui__lock);
		if (err < 0)
			break;

		FD_ZERO(&read_set);
		FD_SET(0, &read_set);

		if (delay_secs) {
			timeout.tv_sec = delay_secs;
			timeout.tv_usec = 0;
		}

	        err = select(1, &read_set, NULL, NULL, ptimeout);
		if (err > 0 && FD_ISSET(0, &read_set))
			key = newtGetKey();
		else if (err == 0)
			break;
		else {
			pthread_mutex_lock(&ui__lock);
			SLtt_get_screen_size();
			SLsmg_reinit_smg();
			pthread_mutex_unlock(&ui__lock);
			ui_browser__refresh_dimensions(self);
			__ui_browser__show_title(self, self->title);
			ui_helpline__puts(self->helpline);
			continue;
		}

		switch (key) {
		case NEWT_KEY_DOWN:
			if (self->index == self->nr_entries - 1)
				break;
			++self->index;
			if (self->index == self->top_idx + self->height) {
				++self->top_idx;
				self->seek(self, +1, SEEK_CUR);
			}
			break;
		case NEWT_KEY_UP:
			if (self->index == 0)
				break;
			--self->index;
			if (self->index < self->top_idx) {
				--self->top_idx;
				self->seek(self, -1, SEEK_CUR);
			}
			break;
		case NEWT_KEY_PGDN:
		case ' ':
			if (self->top_idx + self->height > self->nr_entries - 1)
				break;

			offset = self->height;
			if (self->index + offset > self->nr_entries - 1)
				offset = self->nr_entries - 1 - self->index;
			self->index += offset;
			self->top_idx += offset;
			self->seek(self, +offset, SEEK_CUR);
			break;
		case NEWT_KEY_PGUP:
			if (self->top_idx == 0)
				break;

			if (self->top_idx < self->height)
				offset = self->top_idx;
			else
				offset = self->height;

			self->index -= offset;
			self->top_idx -= offset;
			self->seek(self, -offset, SEEK_CUR);
			break;
		case NEWT_KEY_HOME:
			ui_browser__reset_index(self);
			break;
		case NEWT_KEY_END:
			offset = self->height - 1;
			if (offset >= self->nr_entries)
				offset = self->nr_entries - 1;

			self->index = self->nr_entries - 1;
			self->top_idx = self->index - offset;
			self->seek(self, -offset, SEEK_END);
			break;
		default:
			return key;
		}
	}
	return -1;
}

unsigned int ui_browser__list_head_refresh(struct ui_browser *self)
{
	struct list_head *pos;
	struct list_head *head = self->entries;
	int row = 0;

	if (self->top == NULL || self->top == self->entries)
                self->top = ui_browser__list_head_filter_entries(self, head->next);

	pos = self->top;

	list_for_each_from(pos, head) {
		if (!self->filter || !self->filter(self, pos)) {
			ui_browser__gotorc(self, row, 0);
			self->write(self, pos, row);
			if (++row == self->height)
				break;
		}
	}

	return row;
}

static struct ui_browser__colors {
	const char *topColorFg, *topColorBg;
	const char *mediumColorFg, *mediumColorBg;
	const char *normalColorFg, *normalColorBg;
	const char *selColorFg, *selColorBg;
	const char *codeColorFg, *codeColorBg;
} ui_browser__default_colors = {
	"red",       "lightgray",
	"green",     "lightgray",
	"black",     "lightgray",
	"lightgray", "magenta",
	"blue",	     "lightgray",
};

void ui_browser__init(void)
{
	struct ui_browser__colors *c = &ui_browser__default_colors;

	sltt_set_color(HE_COLORSET_TOP, NULL, c->topColorFg, c->topColorBg);
	sltt_set_color(HE_COLORSET_MEDIUM, NULL, c->mediumColorFg, c->mediumColorBg);
	sltt_set_color(HE_COLORSET_NORMAL, NULL, c->normalColorFg, c->normalColorBg);
	sltt_set_color(HE_COLORSET_SELECTED, NULL, c->selColorFg, c->selColorBg);
	sltt_set_color(HE_COLORSET_CODE, NULL, c->codeColorFg, c->codeColorBg);
}
