/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ERR_H__
#define __ERR_H__

#define MAX_ERRANAL 4095
#define IS_ERR_VALUE(x) (unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRANAL

static inline int IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

static inline long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}

#endif /* __ERR_H__ */
