#ifndef __UDF_ENDIAN_H
#define __UDF_ENDIAN_H

#include <asm/byteorder.h>
#include <linux/string.h>

static inline kernel_lb_addr lelb_to_cpu(lb_addr in)
{
	kernel_lb_addr out;
	out.logicalBlockNum = le32_to_cpu(in.logicalBlockNum);
	out.partitionReferenceNum = le16_to_cpu(in.partitionReferenceNum);
	return out;
}

static inline lb_addr cpu_to_lelb(kernel_lb_addr in)
{
	lb_addr out;
	out.logicalBlockNum = cpu_to_le32(in.logicalBlockNum);
	out.partitionReferenceNum = cpu_to_le16(in.partitionReferenceNum);
	return out;
}

static inline kernel_timestamp lets_to_cpu(timestamp in)
{
	kernel_timestamp out;
	memcpy(&out, &in, sizeof(timestamp));
	out.typeAndTimezone = le16_to_cpu(in.typeAndTimezone);
	out.year = le16_to_cpu(in.year);
	return out;
}

static inline short_ad lesa_to_cpu(short_ad in)
{
	short_ad out;
	out.extLength = le32_to_cpu(in.extLength);
	out.extPosition = le32_to_cpu(in.extPosition);
	return out;
}

static inline short_ad cpu_to_lesa(short_ad in)
{
	short_ad out;
	out.extLength = cpu_to_le32(in.extLength);
	out.extPosition = cpu_to_le32(in.extPosition);
	return out;
}

static inline kernel_long_ad lela_to_cpu(long_ad in)
{
	kernel_long_ad out;
	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = lelb_to_cpu(in.extLocation);
	return out;
}

static inline long_ad cpu_to_lela(kernel_long_ad in)
{
	long_ad out;
	out.extLength = cpu_to_le32(in.extLength);
	out.extLocation = cpu_to_lelb(in.extLocation);
	return out;
}

static inline kernel_extent_ad leea_to_cpu(extent_ad in)
{
	kernel_extent_ad out;
	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = le32_to_cpu(in.extLocation);
	return out;
}

static inline timestamp cpu_to_lets(kernel_timestamp in)
{
	timestamp out;
	memcpy(&out, &in, sizeof(timestamp));
	out.typeAndTimezone = cpu_to_le16(in.typeAndTimezone);
	out.year = cpu_to_le16(in.year);
	return out;
}

#endif				/* __UDF_ENDIAN_H */
