/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_UI_BROWSER_H_
#define _PERF_UI_BROWSER_H_ 1

#include <linux/types.h>
#include <stdarg.h>
#include <sys/types.h>

#define HE_COLORSET_TOP		50
#define HE_COLORSET_MEDIUM	51
#define HE_COLORSET_NORMAL	52
#define HE_COLORSET_SELECTED	53
#define HE_COLORSET_JUMP_ARROWS	54
#define HE_COLORSET_ADDR	55
#define HE_COLORSET_ROOT	56

struct ui_browser {
	u64	      index, top_idx;
	void	      *top, *entries;
	u16	      y, x, width, height, rows, columns, horiz_scroll;
	u8	      extra_title_lines;
	int	      current_color;
	void	      *priv;
	char	      *title;
	char	      *helpline;
	const char    *no_samples_msg;
	void 	      (*refresh_dimensions)(struct ui_browser *browser);
	unsigned int  (*refresh)(struct ui_browser *browser);
	void	      (*write)(struct ui_browser *browser, void *entry, int row);
	void	      (*seek)(struct ui_browser *browser, off_t offset, int whence);
	bool	      (*filter)(struct ui_browser *browser, void *entry);
	u32	      nr_entries;
	bool	      navkeypressed;
	bool	      use_navkeypressed;
};

int  ui_browser__set_color(struct ui_browser *browser, int color);
void ui_browser__set_percent_color(struct ui_browser *browser,
				   double percent, bool current);
bool ui_browser__is_current_entry(struct ui_browser *browser, unsigned row);
void ui_browser__refresh_dimensions(struct ui_browser *browser);
void ui_browser__reset_index(struct ui_browser *browser);

void ui_browser__gotorc_title(struct ui_browser *browser, int y, int x);
void ui_browser__gotorc(struct ui_browser *browser, int y, int x);
void ui_browser__write_nstring(struct ui_browser *browser, const char *msg,
			       unsigned int width);
void ui_browser__vprintf(struct ui_browser *browser, const char *fmt, va_list args);
void ui_browser__printf(struct ui_browser *browser, const char *fmt, ...);
void ui_browser__write_graph(struct ui_browser *browser, int graph);
void __ui_browser__line_arrow(struct ui_browser *browser, unsigned int column,
			      u64 start, u64 end);
void ui_browser__mark_fused(struct ui_browser *browser, unsigned int column,
			    unsigned int row, int diff, bool arrow_down);
void __ui_browser__show_title(struct ui_browser *browser, const char *title);
void ui_browser__show_title(struct ui_browser *browser, const char *title);
int ui_browser__show(struct ui_browser *browser, const char *title,
		     const char *helpline, ...);
void ui_browser__hide(struct ui_browser *browser);
int ui_browser__refresh(struct ui_browser *browser);
int ui_browser__run(struct ui_browser *browser, int delay_secs);
void ui_browser__update_nr_entries(struct ui_browser *browser, u32 nr_entries);
void ui_browser__handle_resize(struct ui_browser *browser);
void __ui_browser__vline(struct ui_browser *browser, unsigned int column,
			 u16 start, u16 end);

int ui_browser__warning(struct ui_browser *browser, int timeout,
			const char *format, ...);
int ui_browser__warn_unhandled_hotkey(struct ui_browser *browser, int key, int timeout, const char *help);
int ui_browser__help_window(struct ui_browser *browser, const char *text);
bool ui_browser__dialog_yesno(struct ui_browser *browser, const char *text);
int ui_browser__input_window(const char *title, const char *text, char *input,
			     const char *exit_msg, int delay_sec);
struct perf_session;
int tui__header_window(struct perf_session *session);

void ui_browser__argv_seek(struct ui_browser *browser, off_t offset, int whence);
unsigned int ui_browser__argv_refresh(struct ui_browser *browser);

void ui_browser__rb_tree_seek(struct ui_browser *browser, off_t offset, int whence);
unsigned int ui_browser__rb_tree_refresh(struct ui_browser *browser);

void ui_browser__list_head_seek(struct ui_browser *browser, off_t offset, int whence);
unsigned int ui_browser__list_head_refresh(struct ui_browser *browser);

void ui_browser__init(void);
#endif /* _PERF_UI_BROWSER_H_ */
