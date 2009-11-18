#include "cache.h"
#include "color.h"

int perf_use_color_default = -1;

static int parse_color(const char *name, int len)
{
	static const char * const color_names[] = {
		"normal", "black", "red", "green", "yellow",
		"blue", "magenta", "cyan", "white"
	};
	char *end;
	int i;

	for (i = 0; i < (int)ARRAY_SIZE(color_names); i++) {
		const char *str = color_names[i];
		if (!strncasecmp(name, str, len) && !str[len])
			return i - 1;
	}
	i = strtol(name, &end, 10);
	if (end - name == len && i >= -1 && i <= 255)
		return i;
	return -2;
}

static int parse_attr(const char *name, int len)
{
	static const int attr_values[] = { 1, 2, 4, 5, 7 };
	static const char * const attr_names[] = {
		"bold", "dim", "ul", "blink", "reverse"
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(attr_names); i++) {
		const char *str = attr_names[i];
		if (!strncasecmp(name, str, len) && !str[len])
			return attr_values[i];
	}
	return -1;
}

void color_parse(const char *value, const char *var, char *dst)
{
	color_parse_mem(value, strlen(value), var, dst);
}

void color_parse_mem(const char *value, int value_len, const char *var,
		char *dst)
{
	const char *ptr = value;
	int len = value_len;
	int attr = -1;
	int fg = -2;
	int bg = -2;

	if (!strncasecmp(value, "reset", len)) {
		strcpy(dst, PERF_COLOR_RESET);
		return;
	}

	/* [fg [bg]] [attr] */
	while (len > 0) {
		const char *word = ptr;
		int val, wordlen = 0;

		while (len > 0 && !isspace(word[wordlen])) {
			wordlen++;
			len--;
		}

		ptr = word + wordlen;
		while (len > 0 && isspace(*ptr)) {
			ptr++;
			len--;
		}

		val = parse_color(word, wordlen);
		if (val >= -1) {
			if (fg == -2) {
				fg = val;
				continue;
			}
			if (bg == -2) {
				bg = val;
				continue;
			}
			goto bad;
		}
		val = parse_attr(word, wordlen);
		if (val < 0 || attr != -1)
			goto bad;
		attr = val;
	}

	if (attr >= 0 || fg >= 0 || bg >= 0) {
		int sep = 0;

		*dst++ = '\033';
		*dst++ = '[';
		if (attr >= 0) {
			*dst++ = '0' + attr;
			sep++;
		}
		if (fg >= 0) {
			if (sep++)
				*dst++ = ';';
			if (fg < 8) {
				*dst++ = '3';
				*dst++ = '0' + fg;
			} else {
				dst += sprintf(dst, "38;5;%d", fg);
			}
		}
		if (bg >= 0) {
			if (sep++)
				*dst++ = ';';
			if (bg < 8) {
				*dst++ = '4';
				*dst++ = '0' + bg;
			} else {
				dst += sprintf(dst, "48;5;%d", bg);
			}
		}
		*dst++ = 'm';
	}
	*dst = 0;
	return;
bad:
	die("bad color value '%.*s' for variable '%s'", value_len, value, var);
}

int perf_config_colorbool(const char *var, const char *value, int stdout_is_tty)
{
	if (value) {
		if (!strcasecmp(value, "never"))
			return 0;
		if (!strcasecmp(value, "always"))
			return 1;
		if (!strcasecmp(value, "auto"))
			goto auto_color;
	}

	/* Missing or explicit false to turn off colorization */
	if (!perf_config_bool(var, value))
		return 0;

	/* any normal truth value defaults to 'auto' */
 auto_color:
	if (stdout_is_tty < 0)
		stdout_is_tty = isatty(1);
	if (stdout_is_tty || (pager_in_use() && pager_use_color)) {
		char *term = getenv("TERM");
		if (term && strcmp(term, "dumb"))
			return 1;
	}
	return 0;
}

int perf_color_default_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "color.ui")) {
		perf_use_color_default = perf_config_colorbool(var, value, -1);
		return 0;
	}

	return perf_default_config(var, value, cb);
}

static int __color_vfprintf(FILE *fp, const char *color, const char *fmt,
		va_list args, const char *trail)
{
	int r = 0;

	/*
	 * Auto-detect:
	 */
	if (perf_use_color_default < 0) {
		if (isatty(1) || pager_in_use())
			perf_use_color_default = 1;
		else
			perf_use_color_default = 0;
	}

	if (perf_use_color_default && *color)
		r += fprintf(fp, "%s", color);
	r += vfprintf(fp, fmt, args);
	if (perf_use_color_default && *color)
		r += fprintf(fp, "%s", PERF_COLOR_RESET);
	if (trail)
		r += fprintf(fp, "%s", trail);
	return r;
}

int color_vfprintf(FILE *fp, const char *color, const char *fmt, va_list args)
{
	return __color_vfprintf(fp, color, fmt, args, NULL);
}


int color_fprintf(FILE *fp, const char *color, const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = color_vfprintf(fp, color, fmt, args);
	va_end(args);
	return r;
}

int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...)
{
	va_list args;
	int r;
	va_start(args, fmt);
	r = __color_vfprintf(fp, color, fmt, args, "\n");
	va_end(args);
	return r;
}

/*
 * This function splits the buffer by newlines and colors the lines individually.
 *
 * Returns 0 on success.
 */
int color_fwrite_lines(FILE *fp, const char *color,
		size_t count, const char *buf)
{
	if (!*color)
		return fwrite(buf, count, 1, fp) != 1;

	while (count) {
		char *p = memchr(buf, '\n', count);

		if (p != buf && (fputs(color, fp) < 0 ||
				fwrite(buf, p ? (size_t)(p - buf) : count, 1, fp) != 1 ||
				fputs(PERF_COLOR_RESET, fp) < 0))
			return -1;
		if (!p)
			return 0;
		if (fputc('\n', fp) < 0)
			return -1;
		count -= p + 1 - buf;
		buf = p + 1;
	}
	return 0;
}

const char *get_percent_color(double percent)
{
	const char *color = PERF_COLOR_NORMAL;

	/*
	 * We color high-overhead entries in red, mid-overhead
	 * entries in green - and keep the low overhead places
	 * normal:
	 */
	if (percent >= MIN_RED)
		color = PERF_COLOR_RED;
	else {
		if (percent > MIN_GREEN)
			color = PERF_COLOR_GREEN;
	}
	return color;
}

int percent_color_fprintf(FILE *fp, const char *fmt, double percent)
{
	int r;
	const char *color;

	color = get_percent_color(percent);
	r = color_fprintf(fp, color, fmt, percent);

	return r;
}
