#ifndef __API_DEBUG_INTERNAL_H__
#define __API_DEBUG_INTERNAL_H__

#include "debug.h"

#define __pr(func, fmt, ...)	\
do {				\
	if ((func))		\
		(func)("libapi: " fmt, ##__VA_ARGS__); \
} while (0)

extern libapi_print_fn_t __pr_warning;
extern libapi_print_fn_t __pr_info;
extern libapi_print_fn_t __pr_debug;

#define pr_warning(fmt, ...)	__pr(__pr_warning, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	__pr(__pr_info, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)	__pr(__pr_debug, fmt, ##__VA_ARGS__)

#endif /* __API_DEBUG_INTERNAL_H__ */
