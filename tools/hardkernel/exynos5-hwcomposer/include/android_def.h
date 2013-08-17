#ifndef __ANDROIDDEF_H
#define __ANDROIDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * system/core/include/cutils/native_handle.h
 */
typedef struct native_handle
{
	int version;        /* sizeof(native_handle_t) */
	int numFds;         /* number of file-descriptors at &data[0] */
	int numInts;        /* number of ints at &data[numFds] */
	int data[0];        /* numFds + numInts ints */
} native_handle_t;

/******************************************************************************
 * system/core/include/system/window.h
 */
typedef const native_handle_t* buffer_handle_t;

#include <exynos5410/gralloc_priv.h>

#ifdef __cplusplus
};
#endif

#endif
