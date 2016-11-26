#ifndef PERF_UTIL_CLANG_C_H
#define PERF_UTIL_CLANG_C_H

#ifdef __cplusplus
extern "C" {
#endif

extern void perf_clang__init(void);
extern void perf_clang__cleanup(void);

extern int test__clang_to_IR(void);
extern int test__clang_to_obj(void);

#ifdef __cplusplus
}
#endif
#endif
