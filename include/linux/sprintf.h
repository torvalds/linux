/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_SPRINTF_H_
#define _LINUX_KERNEL_SPRINTF_H_

#include <linux/compiler_attributes.h>
#include <linux/types.h>

int num_to_str(char *buf, int size, unsigned long long num, unsigned int width);

__printf(2, 3) int sprintf(char *buf, const char * fmt, ...);
__printf(2, 0) int vsprintf(char *buf, const char *, va_list);
__printf(3, 4) int snprintf(char *buf, size_t size, const char *fmt, ...);
__printf(3, 0) int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
__printf(3, 4) int scnprintf(char *buf, size_t size, const char *fmt, ...);
__printf(3, 0) int vscnprintf(char *buf, size_t size, const char *fmt, va_list args);
__printf(2, 3) __malloc char *kasprintf(gfp_t gfp, const char *fmt, ...);
__printf(2, 0) __malloc char *kvasprintf(gfp_t gfp, const char *fmt, va_list args);
__printf(2, 0) const char *kvasprintf_const(gfp_t gfp, const char *fmt, va_list args);

__scanf(2, 3) int sscanf(const char *, const char *, ...);
__scanf(2, 0) int vsscanf(const char *, const char *, va_list);

/* These are for specific cases, do not use without real need */
extern bool no_hash_pointers;
int no_hash_pointers_enable(char *str);

/* Used for Rust formatting ('%pA') */
char *rust_fmt_argument(char *buf, char *end, const void *ptr);

#endif	/* _LINUX_KERNEL_SPRINTF_H */
