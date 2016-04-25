#ifndef __API_DEBUG_H__
#define __API_DEBUG_H__

typedef int (*libapi_print_fn_t)(const char *, ...);

void libapi_set_print(libapi_print_fn_t warn,
		      libapi_print_fn_t info,
		      libapi_print_fn_t debug);

#endif /* __API_DEBUG_H__ */
