/*
 * UUID/GUID definition
 *
 * Copyright (C) 2010, 2016 Intel Corp.
 *	Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _LINUX_UUID_H_
#define _LINUX_UUID_H_

#include <uapi/linux/uuid.h>

/*
 * V1 (time-based) UUID definition [RFC 4122].
 * - the timestamp is a 60-bit value, split 32/16/12, and goes in 100ns
 *   increments since midnight 15th October 1582
 *   - add AFS_UUID_TO_UNIX_TIME to convert unix time in 100ns units to UUID
 *     time
 * - the clock sequence is a 14-bit counter to avoid duplicate times
 */
struct uuid_v1 {
	__be32		time_low;			/* low part of timestamp */
	__be16		time_mid;			/* mid part of timestamp */
	__be16		time_hi_and_version;		/* high part of timestamp and version  */
#define UUID_TO_UNIX_TIME	0x01b21dd213814000ULL
#define UUID_TIMEHI_MASK	0x0fff
#define UUID_VERSION_TIME	0x1000	/* time-based UUID */
#define UUID_VERSION_NAME	0x3000	/* name-based UUID */
#define UUID_VERSION_RANDOM	0x4000	/* (pseudo-)random generated UUID */
	u8		clock_seq_hi_and_reserved;	/* clock seq hi and variant */
#define UUID_CLOCKHI_MASK	0x3f
#define UUID_VARIANT_STD	0x80
	u8		clock_seq_low;			/* clock seq low */
	u8		node[6];			/* spatially unique node ID (MAC addr) */
};

/*
 * The length of a UUID string ("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
 * not including trailing NUL.
 */
#define	UUID_STRING_LEN		36

static inline int uuid_le_cmp(const uuid_le u1, const uuid_le u2)
{
	return memcmp(&u1, &u2, sizeof(uuid_le));
}

static inline int uuid_be_cmp(const uuid_be u1, const uuid_be u2)
{
	return memcmp(&u1, &u2, sizeof(uuid_be));
}

void generate_random_uuid(unsigned char uuid[16]);

extern void uuid_le_gen(uuid_le *u);
extern void uuid_be_gen(uuid_be *u);

bool __must_check uuid_is_valid(const char *uuid);

extern const u8 uuid_le_index[16];
extern const u8 uuid_be_index[16];

int uuid_le_to_bin(const char *uuid, uuid_le *u);
int uuid_be_to_bin(const char *uuid, uuid_be *u);

#endif
