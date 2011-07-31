#include <stdlib.h>
#include <newt.h>
#include "../cache.h"
#include "progress.h"

struct ui_progress {
	newtComponent form, scale;
};

struct ui_progress *ui_progress__new(const char *title, u64 total)
{
	struct ui_progress *self = malloc(sizeof(*self));

	if (self != NULL) {
		int cols;

		if (use_browser <= 0)
			return self;
		newtGetScreenSize(&cols, NULL);
		cols -= 4;
		newtCenteredWindow(cols, 1, title);
		self->form  = newtForm(NULL, NULL, 0);
		if (self->form == NULL)
			goto out_free_self;
		self->scale = newtScale(0, 0, cols, total);
		if (self->scale == NULL)
			goto out_free_form;
		newtFormAddComponent(self->form, self->scale);
		newtRefresh();
	}

	return self;

out_free_form:
	newtFormDestroy(self->form);
out_free_self:
	free(self);
	return NULL;
}

void ui_progress__update(struct ui_progress *self, u64 curr)
{
	/*
	 * FIXME: We should have a per UI backend way of showing progress,
	 * stdio will just show a percentage as NN%, etc.
	 */
	if (use_browser <= 0)
		return;
	newtScaleSet(self->scale, curr);
	newtRefresh();
}

void ui_progress__delete(struct ui_progress *self)
{
	if (use_browser > 0) {
		newtFormDestroy(self->form);
		newtPopWindow();
	}
	free(self);
}
