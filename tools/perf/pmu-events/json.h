#ifndef JSON_H
#define JSON_H 1

#include "jsmn.h"

jsmntok_t *parse_json(const char *fn, char **map, size_t *size, int *len);
void free_json(char *map, size_t size, jsmntok_t *tokens);
int json_line(char *map, jsmntok_t *t);
const char *json_name(jsmntok_t *t);
int json_streq(char *map, jsmntok_t *t, const char *s);
int json_len(jsmntok_t *t);

extern int verbose;

#include <stdbool.h>

extern int eprintf(int level, int var, const char *fmt, ...);
#define pr_fmt(fmt)	fmt

#define pr_err(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)

#define pr_info(fmt, ...) \
	eprintf(1, verbose, pr_fmt(fmt), ##__VA_ARGS__)

#define pr_debug(fmt, ...) \
	eprintf(2, verbose, pr_fmt(fmt), ##__VA_ARGS__)

#ifndef roundup
#define roundup(x, y) (                                \
{                                                      \
        const typeof(y) __y = y;                       \
        (((x) + (__y - 1)) / __y) * __y;               \
}                                                      \
)
#endif

#endif
