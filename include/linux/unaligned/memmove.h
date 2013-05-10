#ifndef _LINUX_UNALIGNED_MEMMOVE_H
#define _LINUX_UNALIGNED_MEMMOVE_H

#include <linux/kernel.h>
#include <linux/string.h>

/* Use memmove here, so gcc does not insert a __builtin_memcpy. */

static inline u16 __get_unaligned_memmove16(const void *p)
{
	u16 tmp;
	memmove(&tmp, p, 2);
	return tmp;
}

static inline u32 __get_unaligned_memmove32(const void *p)
{
	u32 tmp;
	memmove(&tmp, p, 4);
	return tmp;
}

static inline u64 __get_unaligned_memmove64(const void *p)
{
	u64 tmp;
	memmove(&tmp, p, 8);
	return tmp;
}

static inline void __put_unaligned_memmove16(u16 val, void *p)
{
	memmove(p, &val, 2);
}

static inline void __put_unaligned_memmove32(u32 val, void *p)
{
	memmove(p, &val, 4);
}

static inline void __put_unaligned_memmove64(u64 val, void *p)
{
	memmove(p, &val, 8);
}

#endif /* _LINUX_UNALIGNED_MEMMOVE_H */
