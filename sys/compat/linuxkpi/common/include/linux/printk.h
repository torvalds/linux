/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _LINUX_PRINTK_H_
#define	_LINUX_PRINTK_H_

#include <linux/kernel.h>

/* GID printing macros */
#define	GID_PRINT_FMT			"%.4x:%.4x:%.4x:%.4x:%.4x:%.4x:%.4x:%.4x"
#define	GID_PRINT_ARGS(gid_raw)		htons(((u16 *)gid_raw)[0]), htons(((u16 *)gid_raw)[1]),\
					htons(((u16 *)gid_raw)[2]), htons(((u16 *)gid_raw)[3]),\
					htons(((u16 *)gid_raw)[4]), htons(((u16 *)gid_raw)[5]),\
					htons(((u16 *)gid_raw)[6]), htons(((u16 *)gid_raw)[7])

enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET
};

static inline void
print_hex_dump(const char *level, const char *prefix_str,
    const int prefix_type, const int rowsize, const int groupsize,
    const void *buf, size_t len, const bool ascii)
{
	typedef const struct { long long value; } __packed *print_64p_t;
	typedef const struct { uint32_t value; } __packed *print_32p_t;
	typedef const struct { uint16_t value; } __packed *print_16p_t;
	const void *buf_old = buf;
	int row;

	while (len > 0) {
		if (level != NULL)
			printf("%s", level);
		if (prefix_str != NULL)
			printf("%s ", prefix_str);

		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			printf("[%p] ", buf);
			break;
		case DUMP_PREFIX_OFFSET:
			printf("[%p] ", (const char *)((const char *)buf -
			    (const char *)buf_old));
			break;
		default:
			break;
		}
		for (row = 0; row != rowsize; row++) {
			if (groupsize == 8 && len > 7) {
				printf("%016llx ", ((print_64p_t)buf)->value);
				buf = (const uint8_t *)buf + 8;
				len -= 8;
			} else if (groupsize == 4 && len > 3) {
				printf("%08x ", ((print_32p_t)buf)->value);
				buf = (const uint8_t *)buf + 4;
				len -= 4;
			} else if (groupsize == 2 && len > 1) {
				printf("%04x ", ((print_16p_t)buf)->value);
				buf = (const uint8_t *)buf + 2;
				len -= 2;
			} else if (len > 0) {
				printf("%02x ", *(const uint8_t *)buf);
				buf = (const uint8_t *)buf + 1;
				len--;
			} else {
				break;
			}
		}
		printf("\n");
	}
}

static inline void
print_hex_dump_bytes(const char *prefix_str, const int prefix_type,
    const void *buf, size_t len)
{
	print_hex_dump(NULL, prefix_str, prefix_type, 16, 1, buf, len, 0);
}

#define	printk_ratelimit() ({			\
	static linux_ratelimit_t __ratelimited;	\
	linux_ratelimited(&__ratelimited);	\
})

#define	printk_ratelimited(...) ({		\
	bool __retval = printk_ratelimit();	\
	if (__retval)				\
		printk(__VA_ARGS__);		\
	__retval;				\
})

#define	pr_err_ratelimited(fmt, ...) \
	printk_ratelimited(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)

#define	pr_info_ratelimited(fmt, ...) \
	printk_ratelimited(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

#endif					/* _LINUX_PRINTK_H_ */
