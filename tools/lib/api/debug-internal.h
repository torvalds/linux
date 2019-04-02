/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __API_DE_INTERNAL_H__
#define __API_DE_INTERNAL_H__

#include "de.h"

#define __pr(func, fmt, ...)	\
do {				\
	if ((func))		\
		(func)("libapi: " fmt, ##__VA_ARGS__); \
} while (0)

extern libapi_print_fn_t __pr_warning;
extern libapi_print_fn_t __pr_info;
extern libapi_print_fn_t __pr_de;

#define pr_warning(fmt, ...)	__pr(__pr_warning, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	__pr(__pr_info, fmt, ##__VA_ARGS__)
#define pr_de(fmt, ...)	__pr(__pr_de, fmt, ##__VA_ARGS__)

#endif /* __API_DE_INTERNAL_H__ */
