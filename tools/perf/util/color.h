#ifndef COLOR_H
#define COLOR_H

/* "\033[1;38;5;2xx;48;5;2xxm\0" is 23 bytes */
#define COLOR_MAXLEN 24

#define PERF_COLOR_NORMAL	""
#define PERF_COLOR_RESET	"\033[m"
#define PERF_COLOR_BOLD		"\033[1m"
#define PERF_COLOR_RED		"\033[31m"
#define PERF_COLOR_GREEN	"\033[32m"
#define PERF_COLOR_YELLOW	"\033[33m"
#define PERF_COLOR_BLUE		"\033[34m"
#define PERF_COLOR_MAGENTA	"\033[35m"
#define PERF_COLOR_CYAN		"\033[36m"
#define PERF_COLOR_BG_RED	"\033[41m"

/*
 * This variable stores the value of color.ui
 */
extern int perf_use_color_default;


/*
 * Use this instead of perf_default_config if you need the value of color.ui.
 */
int perf_color_default_config(const char *var, const char *value, void *cb);

int perf_config_colorbool(const char *var, const char *value, int stdout_is_tty);
void color_parse(const char *value, const char *var, char *dst);
void color_parse_mem(const char *value, int len, const char *var, char *dst);
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);
int color_fwrite_lines(FILE *fp, const char *color, size_t count, const char *buf);

#endif /* COLOR_H */
