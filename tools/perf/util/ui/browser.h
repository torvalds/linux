#ifndef _PERF_UI_BROWSER_H_
#define _PERF_UI_BROWSER_H_ 1

#include <stdbool.h>
#include <sys/types.h>
#include "../types.h"

#define HE_COLORSET_TOP		50
#define HE_COLORSET_MEDIUM	51
#define HE_COLORSET_NORMAL	52
#define HE_COLORSET_SELECTED	53
#define HE_COLORSET_CODE	54

struct ui_browser {
	u64	      index, top_idx;
	void	      *top, *entries;
	u16	      y, x, width, height;
	void	      *priv;
	const char    *title;
	char	      *helpline;
	unsigned int  (*refresh)(struct ui_browser *self);
	void	      (*write)(struct ui_browser *self, void *entry, int row);
	void	      (*seek)(struct ui_browser *self, off_t offset, int whence);
	bool	      (*filter)(struct ui_browser *self, void *entry);
	u32	      nr_entries;
	bool	      navkeypressed;
	bool	      use_navkeypressed;
};

void ui_browser__set_color(struct ui_browser *self, int color);
void ui_browser__set_percent_color(struct ui_browser *self,
				   double percent, bool current);
bool ui_browser__is_current_entry(struct ui_browser *self, unsigned row);
void ui_browser__refresh_dimensions(struct ui_browser *self);
void ui_browser__reset_index(struct ui_browser *self);

void ui_browser__gotorc(struct ui_browser *self, int y, int x);
void __ui_browser__show_title(struct ui_browser *browser, const char *title);
void ui_browser__show_title(struct ui_browser *browser, const char *title);
int ui_browser__show(struct ui_browser *self, const char *title,
		     const char *helpline, ...);
void ui_browser__hide(struct ui_browser *self);
int ui_browser__refresh(struct ui_browser *self);
int ui_browser__run(struct ui_browser *browser, int delay_secs);
void ui_browser__update_nr_entries(struct ui_browser *browser, u32 nr_entries);
void ui_browser__handle_resize(struct ui_browser *browser);

int ui_browser__warning(struct ui_browser *browser, int timeout,
			const char *format, ...);
int ui_browser__help_window(struct ui_browser *browser, const char *text);
bool ui_browser__dialog_yesno(struct ui_browser *browser, const char *text);

void ui_browser__argv_seek(struct ui_browser *browser, off_t offset, int whence);
unsigned int ui_browser__argv_refresh(struct ui_browser *browser);

void ui_browser__rb_tree_seek(struct ui_browser *self, off_t offset, int whence);
unsigned int ui_browser__rb_tree_refresh(struct ui_browser *self);

void ui_browser__list_head_seek(struct ui_browser *self, off_t offset, int whence);
unsigned int ui_browser__list_head_refresh(struct ui_browser *self);

void ui_browser__init(void);
#endif /* _PERF_UI_BROWSER_H_ */
