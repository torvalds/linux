#ifndef __UDF_ENDIAN_H
#define __UDF_ENDIAN_H

#include <asm/byteorder.h>
#include <linux/string.h>

static inline struct kernel_lb_addr lelb_to_cpu(struct lb_addr in)
{
	struct kernel_lb_addr out;

	out.logicalBlockNum = le32_to_cpu(in.logicalBlockNum);
	out.partitionReferenceNum = le16_to_cpu(in.partitionReferenceNum);

	return out;
}

static inline struct lb_addr cpu_to_lelb(struct kernel_lb_addr in)
{
	struct lb_addr out;

	out.logicalBlockNum = cpu_to_le32(in.logicalBlockNum);
	out.partitionReferenceNum = cpu_to_le16(in.partitionReferenceNum);

	return out;
}

static inline struct short_ad lesa_to_cpu(struct short_ad in)
{
	struct short_ad out;

	out.extLength = le32_to_cpu(in.extLength);
	out.extPosition = le32_to_cpu(in.extPosition);

	return out;
}

static inline struct short_ad cpu_to_lesa(struct short_ad in)
{
	struct short_ad out;

	out.extLength = cpu_to_le32(in.extLength);
	out.extPosition = cpu_to_le32(in.extPosition);

	return out;
}

static inline struct kernel_long_ad lela_to_cpu(struct long_ad in)
{
	struct kernel_long_ad out;

	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = lelb_to_cpu(in.extLocation);

	return out;
}

static inline struct long_ad cpu_to_lela(struct kernel_long_ad in)
{
	struct long_ad out;

	out.extLength = cpu_to_le32(in.extLength);
	out.extLocation = cpu_to_lelb(in.extLocation);

	return out;
}

static inline struct kernel_extent_ad leea_to_cpu(struct extent_ad in)
{
	struct kernel_extent_ad out;

	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = le32_to_cpu(in.extLocation);

	return out;
}

#endif /* __UDF_ENDIAN_H */
