/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Follows implementation found in linux/virtio_byteorder.h
 */
#ifndef _LINUX_RPMSG_BYTEORDER_H
#define _LINUX_RPMSG_BYTEORDER_H
#include <linux/types.h>
#include <uapi/linux/rpmsg_types.h>

static inline bool rpmsg_is_little_endian(void)
{
#ifdef __LITTLE_ENDIAN
	return true;
#else
	return false;
#endif
}

static inline u16 __rpmsg16_to_cpu(bool little_endian, __rpmsg16 val)
{
	if (little_endian)
		return le16_to_cpu((__force __le16)val);
	else
		return be16_to_cpu((__force __be16)val);
}

static inline __rpmsg16 __cpu_to_rpmsg16(bool little_endian, u16 val)
{
	if (little_endian)
		return (__force __rpmsg16)cpu_to_le16(val);
	else
		return (__force __rpmsg16)cpu_to_be16(val);
}

static inline u32 __rpmsg32_to_cpu(bool little_endian, __rpmsg32 val)
{
	if (little_endian)
		return le32_to_cpu((__force __le32)val);
	else
		return be32_to_cpu((__force __be32)val);
}

static inline __rpmsg32 __cpu_to_rpmsg32(bool little_endian, u32 val)
{
	if (little_endian)
		return (__force __rpmsg32)cpu_to_le32(val);
	else
		return (__force __rpmsg32)cpu_to_be32(val);
}

static inline u64 __rpmsg64_to_cpu(bool little_endian, __rpmsg64 val)
{
	if (little_endian)
		return le64_to_cpu((__force __le64)val);
	else
		return be64_to_cpu((__force __be64)val);
}

static inline __rpmsg64 __cpu_to_rpmsg64(bool little_endian, u64 val)
{
	if (little_endian)
		return (__force __rpmsg64)cpu_to_le64(val);
	else
		return (__force __rpmsg64)cpu_to_be64(val);
}

#endif /* _LINUX_RPMSG_BYTEORDER_H */
