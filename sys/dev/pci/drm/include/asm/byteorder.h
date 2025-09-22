/* Public domain. */

#ifndef _ASM_BYTEORDER_H
#define _ASM_BYTEORDER_H

#include <sys/endian.h>
#include <linux/types.h>

#define le16_to_cpu(x) letoh16(x)
#define le32_to_cpu(x) letoh32(x)
#define le64_to_cpu(x) letoh64(x)
#define be16_to_cpu(x) betoh16(x)
#define be32_to_cpu(x) betoh32(x)
#define be64_to_cpu(x) betoh64(x)
#define le16_to_cpup(x)	lemtoh16(x)
#define le32_to_cpup(x)	lemtoh32(x)
#define le64_to_cpup(x)	lemtoh64(x)
#define be16_to_cpup(x)	bemtoh16(x)
#define be32_to_cpup(x)	bemtoh32(x)
#define be64_to_cpup(x)	bemtoh64(x)
#define get_unaligned_le32(x)	lemtoh32(x)
#define cpu_to_le16(x) htole16(x)
#define cpu_to_le32(x) htole32(x)
#define cpu_to_le64(x) htole64(x)
#define cpu_to_be16(x) htobe16(x)
#define cpu_to_be32(x) htobe32(x)
#define cpu_to_be64(x) htobe64(x)

#define swab16(x) swap16(x)
#define swab32(x) swap32(x)

static inline void
le16_add_cpu(uint16_t *p, uint16_t n)
{
	htolem16(p, lemtoh16(p) + n);
}

#endif
