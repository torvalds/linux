/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __API_DE_H__
#define __API_DE_H__

typedef int (*libapi_print_fn_t)(const char *, ...);

void libapi_set_print(libapi_print_fn_t warn,
		      libapi_print_fn_t info,
		      libapi_print_fn_t de);

#endif /* __API_DE_H__ */
