#ifndef _LINUX_BYTEORDER_PDP_ENDIAN_H
#define _LINUX_BYTEORDER_PDP_ENDIAN_H

/*
 * Could have been named NUXI-endian, but we use the same name as in glibc.
 * hopefully only the PDP and its evolutions (old VAXen in compatibility mode)
 * should ever use this braindead byteorder.
 * This file *should* work, but has not been tested.
 *
 * little-endian is 1234; big-endian is 4321; nuxi/pdp-endian is 3412
 *
 * I thought vaxen were NUXI-endian, but was told they were correct-endian
 * (little-endian), though indeed there existed NUXI-endian machines
 * (DEC PDP-11 and old VAXen in compatibility mode).
 * This makes this file a bit useless, but as a proof-of-concept.
 *
 * But what does a __u64 look like: is it 34127856 or 78563412 ???
 * I don't dare imagine! Hence, no 64-bit byteorder support yet.
 * Hopefully, there 64-bit pdp-endian support shouldn't ever be required.
 *
 */

#ifndef __PDP_ENDIAN
#define __PDP_ENDIAN 3412
#endif
#ifndef __PDP_ENDIAN_BITFIELD
#define __PDP_ENDIAN_BITFIELD
#endif

#include <linux/byteorder/swab.h>
#include <linux/byteorder/swabb.h>

#define __constant_htonl(x) ___constant_swahb32((x))
#define __constant_ntohl(x) ___constant_swahb32((x))
#define __constant_htons(x) ___constant_swab16((x))
#define __constant_ntohs(x) ___constant_swab16((x))
#define __constant_cpu_to_le64(x) I DON'T KNOW
#define __constant_le64_to_cpu(x) I DON'T KNOW
#define __constant_cpu_to_le32(x) ___constant_swahw32((x))
#define __constant_le32_to_cpu(x) ___constant_swahw32((x))
#define __constant_cpu_to_le16(x) ((__u16)(x)
#define __constant_le16_to_cpu(x) ((__u16)(x)
#define __constant_cpu_to_be64(x) I DON'T KNOW
#define __constant_be64_to_cpu(x) I DON'T KNOW
#define __constant_cpu_to_be32(x) ___constant_swahb32((x))
#define __constant_be32_to_cpu(x) ___constant_swahb32((x))
#define __constant_cpu_to_be16(x) ___constant_swab16((x))
#define __constant_be16_to_cpu(x) ___constant_swab16((x))
#define __cpu_to_le64(x) I DON'T KNOW
#define __le64_to_cpu(x) I DON'T KNOW
#define __cpu_to_le32(x) ___swahw32((x))
#define __le32_to_cpu(x) ___swahw32((x))
#define __cpu_to_le16(x) ((__u16)(x)
#define __le16_to_cpu(x) ((__u16)(x)
#define __cpu_to_be64(x) I DON'T KNOW
#define __be64_to_cpu(x) I DON'T KNOW
#define __cpu_to_be32(x) __swahb32((x))
#define __be32_to_cpu(x) __swahb32((x))
#define __cpu_to_be16(x) __swab16((x))
#define __be16_to_cpu(x) __swab16((x))
#define __cpu_to_le64p(x) I DON'T KNOW
#define __le64_to_cpup(x) I DON'T KNOW
#define __cpu_to_le32p(x) ___swahw32p((x))
#define __le32_to_cpup(x) ___swahw32p((x))
#define __cpu_to_le16p(x) (*(__u16*)(x))
#define __le16_to_cpup(x) (*(__u16*)(x))
#define __cpu_to_be64p(x) I DON'T KNOW
#define __be64_to_cpup(x) I DON'T KNOW
#define __cpu_to_be32p(x) __swahb32p((x))
#define __be32_to_cpup(x) __swahb32p((x))
#define __cpu_to_be16p(x) __swab16p((x))
#define __be16_to_cpup(x) __swab16p((x))
#define __cpu_to_le64s(x) I DON'T KNOW
#define __le64_to_cpus(x) I DON'T KNOW
#define __cpu_to_le32s(x) ___swahw32s((x))
#define __le32_to_cpus(x) ___swahw32s((x))
#define __cpu_to_le16s(x) do {} while (0)
#define __le16_to_cpus(x) do {} while (0)
#define __cpu_to_be64s(x) I DON'T KNOW
#define __be64_to_cpus(x) I DON'T KNOW
#define __cpu_to_be32s(x) __swahb32s((x))
#define __be32_to_cpus(x) __swahb32s((x))
#define __cpu_to_be16s(x) __swab16s((x))
#define __be16_to_cpus(x) __swab16s((x))

#include <linux/byteorder/generic.h>

#endif /* _LINUX_BYTEORDER_PDP_ENDIAN_H */
