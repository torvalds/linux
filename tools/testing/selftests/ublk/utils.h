/* SPDX-License-Identifier: GPL-2.0 */
#ifndef KUBLK_UTILS_H
#define KUBLK_UTILS_H

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#ifndef offsetof
#define offsetof(TYPE, MEMBER)  ((size_t)&((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                              \
	unsigned long __mptr = (unsigned long)(ptr);                    \
	((type *)(__mptr - offsetof(type, member))); })
#endif

#define round_up(val, rnd) \
	(((val) + ((rnd) - 1)) & ~((rnd) - 1))

static inline unsigned int ilog2(unsigned int x)
{
	if (x == 0)
		return 0;
	return (sizeof(x) * 8 - 1) - __builtin_clz(x);
}

#define UBLK_DBG_DEV            (1U << 0)
#define UBLK_DBG_THREAD         (1U << 1)
#define UBLK_DBG_IO_CMD         (1U << 2)
#define UBLK_DBG_IO             (1U << 3)
#define UBLK_DBG_CTRL_CMD       (1U << 4)
#define UBLK_LOG                (1U << 5)

extern unsigned int ublk_dbg_mask;

static inline void ublk_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
}

static inline void ublk_log(const char *fmt, ...)
{
	if (ublk_dbg_mask & UBLK_LOG) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
	}
}

static inline void ublk_dbg(int level, const char *fmt, ...)
{
	if (level & ublk_dbg_mask) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
	}
}

#endif
