#include "libslang.h"
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>
#include "browser.h"
#include "helpline.h"
#include "../color.h"
#include "../util.h"
#include <stdio.h>

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

void ui_browser__list_head_seek(struct ui_browser *self, off_t offset, int whence)
{
	struct list_head *head = self->entries;
	struct list_head *pos;

	switch (whence) {
	case SEEK_SET:
		pos = head->next;
		break;
	case SEEK_CUR:
		pos = self->top;
		break;
	case SEEK_END:
		pos = head->prev;
		break;
	default:
		return;
	}

	if (offset > 0) {
		while (offset-- != 0)
			pos = pos->next;
	} else {
		while (offset++ != 0)
			pos = pos->prev;
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
	int cols, rows;
	newtGetScreenSize(&cols, &rows);

	self->width = cols - 1;
	self->height = rows - 2;
	self->y = 1;
	self->x = 0;
}

void ui_browser__reset_index(struct ui_browser *self)
{
	self->index = self->top_idx = 0;
	self->seek(self, 0, SEEK_SET);
}

void ui_browser__add_exit_key(struct ui_browser *self, int key)
{
	newtFormAddHotKey(self->form, key);
}

void ui_browser__add_exit_keys(struct ui_browser *self, int keys[])
{
	int i = 0;

	while (keys[i] && i < 64) {
		ui_browser__add_exit_key(self, keys[i]);
		++i;
	}
}

int ui_browser__show(struct ui_browser *self, const char *title,
		     const char *helpline, ...)
{
	va_list ap;
	int keys[] = { NEWT_KEY_UP, NEWT_KEY_DOWN, NEWT_KEY_PGUP,
		       NEWT_KEY_PGDN, NEWT_KEY_HOME, NEWT_KEY_END, ' ',
		       NEWT_KEY_LEFT, NEWT_KEY_ESCAPE, 'q', CTRL('c'), 0 };

	if (self->form != NULL)
		newtFormDestroy(self->form);

	ui_browser__refresh_dimensions(self);
	self->form = newtForm(NULL, NULL, 0);
	if (self->form == NULL)
		return -1;

	self->sb = newtVerticalScrollbar(self->width, 1, self->height,
					 HE_COLORSET_NORMAL,
					 HE_COLORSET_SELECTED);
	if (self->sb == NULL)
		return -1;

	SLsmg_gotorc(0, 0);
	ui_browser__set_color(self, NEWT_COLORSET_ROOT);
	slsmg_write_nstring(title, self->width);

	ui_browser__add_exit_keys(self, keys);
	newtFormAddComponent(self->form, self->sb);

	va_start(ap, helpline);
	ui_helpline__vpush(helpline, ap);
	va_end(ap);
	return 0;
}

void ui_browser__hide(struct ui_browser *self)
{
	newtFormDestroy(self->form);
	self->form = NULL;
	ui_helpline__pop();
}

int ui_browser__refresh(struct ui_browser *self)
{
	int row;

	newtScrollbarSet(self->sb, self->index, self->nr_entries - 1);
	row = self->refresh(self);
	ui_browser__set_color(self, HE_COLORSET_NORMAL);
	SLsmg_fill_region(self->y + row, self->x,
			  self->height - row, self->width, ' ');

	return 0;
}

int ui_browser__run(struct ui_browser *self)
{
	struct newtExitStruct es;

	if (ui_browser__refresh(self) < 0)
		return -1;

	while (1) {
		off_t offset;

		newtFormRun(self->form, &es);

		if (es.reason != NEWT_EXIT_HOTKEY)
			break;
		switch (es.u.key) {
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
			return es.u.key;
		}
		if (ui_browser__refresh(self) < 0)
			return -1;
	}
	return -1;
}

unsigned int ui_browser__list_head_refresh(struct ui_browser *self)
{
	struct list_head *pos;
	struct list_head *head = self->entries;
	int row = 0;

	if (self->top == NULL || self->top == self->entries)
                self->top = head->next;

	pos = self->top;

	list_for_each_from(pos, head) {
		ui_browser__gotorc(self, row, 0);
		self->write(self, pos, row);
		if (++row == self->height)
			break;
	}

	return row;
}

static struct newtPercentTreeColors {
	const char *topColorFg, *topColorBg;
	const char *mediumColorFg, *mediumColorBg;
	const char *normalColorFg, *normalColorBg;
	const char *selColorFg, *selColorBg;
	const char *codeColorFg, *codeColorBg;
} defaultPercentTreeColors = {
	"red",       "lightgray",
	"green",     "lightgray",
	"black",     "lightgray",
	"lightgray", "magenta",
	"blue",	     "lightgray",
};

void ui_browser__init(void)
{
	struct newtPercentTreeColors *c = &defaultPercentTreeColors;

	sltt_set_color(HE_COLORSET_TOP, NULL, c->topColorFg, c->topColorBg);
	sltt_set_color(HE_COLORSET_MEDIUM, NULL, c->mediumColorFg, c->mediumColorBg);
	sltt_set_color(HE_COLORSET_NORMAL, NULL, c->normalColorFg, c->normalColorBg);
	sltt_set_color(HE_COLORSET_SELECTED, NULL, c->selColorFg, c->selColorBg);
	sltt_set_color(HE_COLORSET_CODE, NULL, c->codeColorFg, c->codeColorBg);
}
