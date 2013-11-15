/*
 * linux/can/gw.h
 *
 * Definitions for CAN frame Gateway/Router/Bridge
 *
 * Author: Oliver Hartkopp <oliver.hartkopp@volkswagen.de>
 * Copyright (c) 2011 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 */

#ifndef CAN_GW_H
#define CAN_GW_H

#include <linux/types.h>
#include <linux/can.h>

struct rtcanmsg {
	__u8  can_family;
	__u8  gwtype;
	__u16 flags;
};

/* CAN gateway types */
enum {
	CGW_TYPE_UNSPEC,
	CGW_TYPE_CAN_CAN,	/* CAN->CAN routing */
	__CGW_TYPE_MAX
};

#define CGW_TYPE_MAX (__CGW_TYPE_MAX - 1)

/* CAN rtnetlink attribute definitions */
enum {
	CGW_UNSPEC,
	CGW_MOD_AND,	/* CAN frame modification binary AND */
	CGW_MOD_OR,	/* CAN frame modification binary OR */
	CGW_MOD_XOR,	/* CAN frame modification binary XOR */
	CGW_MOD_SET,	/* CAN frame modification set alternate values */
	CGW_CS_XOR,	/* set data[] XOR checksum into data[index] */
	CGW_CS_CRC8,	/* set data[] CRC8 checksum into data[index] */
	CGW_HANDLED,	/* number of handled CAN frames */
	CGW_DROPPED,	/* number of dropped CAN frames */
	CGW_SRC_IF,	/* ifindex of source network interface */
	CGW_DST_IF,	/* ifindex of destination network interface */
	CGW_FILTER,	/* specify struct can_filter on source CAN device */
	CGW_DELETED,	/* number of deleted CAN frames (see max_hops param) */
	CGW_LIM_HOPS,	/* limit the number of hops of this specific rule */
	__CGW_MAX
};

#define CGW_MAX (__CGW_MAX - 1)

#define CGW_FLAGS_CAN_ECHO 0x01
#define CGW_FLAGS_CAN_SRC_TSTAMP 0x02
#define CGW_FLAGS_CAN_IIF_TX_OK 0x04

#define CGW_MOD_FUNCS 4 /* AND OR XOR SET */

/* CAN frame elements that are affected by curr. 3 CAN frame modifications */
#define CGW_MOD_ID	0x01
#define CGW_MOD_DLC	0x02
#define CGW_MOD_DATA	0x04

#define CGW_FRAME_MODS 3 /* ID DLC DATA */

#define MAX_MODFUNCTIONS (CGW_MOD_FUNCS * CGW_FRAME_MODS)

struct cgw_frame_mod {
	struct can_frame cf;
	__u8 modtype;
} __attribute__((packed));

#define CGW_MODATTR_LEN sizeof(struct cgw_frame_mod)

struct cgw_csum_xor {
	__s8 from_idx;
	__s8 to_idx;
	__s8 result_idx;
	__u8 init_xor_val;
} __attribute__((packed));

struct cgw_csum_crc8 {
	__s8 from_idx;
	__s8 to_idx;
	__s8 result_idx;
	__u8 init_crc_val;
	__u8 final_xor_val;
	__u8 crctab[256];
	__u8 profile;
	__u8 profile_data[20];
} __attribute__((packed));

/* length of checksum operation parameters. idx = index in CAN frame data[] */
#define CGW_CS_XOR_LEN  sizeof(struct cgw_csum_xor)
#define CGW_CS_CRC8_LEN  sizeof(struct cgw_csum_crc8)

/* CRC8 profiles (compute CRC for additional data elements - see below) */
enum {
	CGW_CRC8PRF_UNSPEC,
	CGW_CRC8PRF_1U8,	/* compute one additional u8 value */
	CGW_CRC8PRF_16U8,	/* u8 value table indexed by data[1] & 0xF */
	CGW_CRC8PRF_SFFID_XOR,	/* (can_id & 0xFF) ^ (can_id >> 8 & 0xFF) */
	__CGW_CRC8PRF_MAX
};

#define CGW_CRC8PRF_MAX (__CGW_CRC8PRF_MAX - 1)

/*
 * CAN rtnetlink attribute contents in detail
 *
 * CGW_XXX_IF (length 4 bytes):
 * Sets an interface index for source/destination network interfaces.
 * For the CAN->CAN gwtype the indices of _two_ CAN interfaces are mandatory.
 *
 * CGW_FILTER (length 8 bytes):
 * Sets a CAN receive filter for the gateway job specified by the
 * struct can_filter described in include/linux/can.h
 *
 * CGW_MOD_(AND|OR|XOR|SET) (length 17 bytes):
 * Specifies a modification that's done to a received CAN frame before it is
 * send out to the destination interface.
 *
 * <struct can_frame> data used as operator
 * <u8> affected CAN frame elements
 *
 * CGW_LIM_HOPS (length 1 byte):
 * Limit the number of hops of this specific rule. Usually the received CAN
 * frame can be processed as much as 'max_hops' times (which is given at module
 * load time of the can-gw module). This value is used to reduce the number of
 * possible hops for this gateway rule to a value smaller then max_hops.
 *
 * CGW_CS_XOR (length 4 bytes):
 * Set a simple XOR checksum starting with an initial value into
 * data[result-idx] using data[start-idx] .. data[end-idx]
 *
 * The XOR checksum is calculated like this:
 *
 * xor = init_xor_val
 *
 * for (i = from_idx .. to_idx)
 *      xor ^= can_frame.data[i]
 *
 * can_frame.data[ result_idx ] = xor
 *
 * CGW_CS_CRC8 (length 282 bytes):
 * Set a CRC8 value into data[result-idx] using a given 256 byte CRC8 table,
 * a given initial value and a defined input data[start-idx] .. data[end-idx].
 * Finally the result value is XOR'ed with the final_xor_val.
 *
 * The CRC8 checksum is calculated like this:
 *
 * crc = init_crc_val
 *
 * for (i = from_idx .. to_idx)
 *      crc = crctab[ crc ^ can_frame.data[i] ]
 *
 * can_frame.data[ result_idx ] = crc ^ final_xor_val
 *
 * The calculated CRC may contain additional source data elements that can be
 * defined in the handling of 'checksum profiles' e.g. shown in AUTOSAR specs
 * like http://www.autosar.org/download/R4.0/AUTOSAR_SWS_E2ELibrary.pdf
 * E.g. the profile_data[] may contain additional u8 values (called DATA_IDs)
 * that are used depending on counter values inside the CAN frame data[].
 * So far only three profiles have been implemented for illustration.
 *
 * Remark: In general the attribute data is a linear buffer.
 *         Beware of sending unpacked or aligned structs!
 */

#endif
