/*
 * Support C++ source use utilities defined in util.h
 */

#ifndef PERF_UTIL_UTIL_CXX_H
#define PERF_UTIL_UTIL_CXX_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Now 'new' is the only C++ keyword found in util.h:
 * in tools/include/linux/rbtree.h
 *
 * Other keywords, like class and delete, should be
 * redefined if necessary.
 */
#define new _new
#include "util.h"
#undef new

#ifdef __cplusplus
}
#endif
#endif
