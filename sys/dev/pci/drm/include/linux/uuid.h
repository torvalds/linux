/* Public domain. */

#ifndef _LINUX_UUID_H
#define _LINUX_UUID_H

#include <linux/string.h>

#define UUID_STRING_LEN 36
#define UUID_SIZE	16

typedef struct {
	uint8_t guid[UUID_SIZE];
} guid_t;

static inline void
import_guid(guid_t *dst, const uint8_t *src)
{
	memcpy(&dst->guid, src, sizeof(dst->guid));
}

static inline void
export_guid(uint8_t *dst, const guid_t *src)
{
	memcpy(dst, &src->guid, sizeof(src->guid));
}

static inline void
guid_copy(guid_t *dst, const guid_t *src)
{
	memcpy(&dst->guid, &src->guid, sizeof(dst->guid));
}

static inline bool
guid_equal(const guid_t *a, const guid_t *b)
{
	return memcmp(&a->guid, &b->guid, sizeof(a->guid)) == 0;
}

static inline bool
guid_is_null(const guid_t *a)
{
	return memchr_inv(&a->guid, 0, sizeof(a->guid)) == NULL;
}

static inline void
guid_gen(guid_t *a)
{
	arc4random_buf(&a->guid, sizeof(a->guid));
	a->guid[6] = (a->guid[6] & 0x0f) | 0x40;
	a->guid[8] = (a->guid[8] & 0x3f) | 0x80;
}

#endif
