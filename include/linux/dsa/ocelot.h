/* SPDX-License-Identifier: GPL-2.0
 * Copyright 2019-2021 NXP
 */

#ifndef _NET_DSA_TAG_OCELOT_H
#define _NET_DSA_TAG_OCELOT_H

#include <linux/packing.h>

#define OCELOT_TAG_LEN			16
#define OCELOT_SHORT_PREFIX_LEN		4
#define OCELOT_LONG_PREFIX_LEN		16
#define OCELOT_TOTAL_TAG_LEN	(OCELOT_SHORT_PREFIX_LEN + OCELOT_TAG_LEN)

/* The CPU injection header and the CPU extraction header can have 3 types of
 * prefixes: long, short and no prefix. The format of the header itself is the
 * same in all 3 cases.
 *
 * Extraction with long prefix:
 *
 * +-------------------+-------------------+------+------+------------+-------+
 * | ff:ff:ff:ff:ff:ff | fe:ff:ff:ff:ff:ff | 8880 | 000a | extraction | frame |
 * |                   |                   |      |      |   header   |       |
 * +-------------------+-------------------+------+------+------------+-------+
 *        48 bits             48 bits      16 bits 16 bits  128 bits
 *
 * Extraction with short prefix:
 *
 *                                         +------+------+------------+-------+
 *                                         | 8880 | 000a | extraction | frame |
 *                                         |      |      |   header   |       |
 *                                         +------+------+------------+-------+
 *                                         16 bits 16 bits  128 bits
 *
 * Extraction with no prefix:
 *
 *                                                       +------------+-------+
 *                                                       | extraction | frame |
 *                                                       |   header   |       |
 *                                                       +------------+-------+
 *                                                          128 bits
 *
 *
 * Injection with long prefix:
 *
 * +-------------------+-------------------+------+------+------------+-------+
 * |      any dmac     |      any smac     | 8880 | 000a | injection  | frame |
 * |                   |                   |      |      |   header   |       |
 * +-------------------+-------------------+------+------+------------+-------+
 *        48 bits             48 bits      16 bits 16 bits  128 bits
 *
 * Injection with short prefix:
 *
 *                                         +------+------+------------+-------+
 *                                         | 8880 | 000a | injection  | frame |
 *                                         |      |      |   header   |       |
 *                                         +------+------+------------+-------+
 *                                         16 bits 16 bits  128 bits
 *
 * Injection with no prefix:
 *
 *                                                       +------------+-------+
 *                                                       | injection  | frame |
 *                                                       |   header   |       |
 *                                                       +------------+-------+
 *                                                          128 bits
 *
 * The injection header looks like this (network byte order, bit 127
 * is part of lowest address byte in memory, bit 0 is part of highest
 * address byte):
 *
 *         +------+------+------+------+------+------+------+------+
 * 127:120 |BYPASS| MASQ |          MASQ_PORT        |REW_OP|REW_OP|
 *         +------+------+------+------+------+------+------+------+
 * 119:112 |                         REW_OP                        |
 *         +------+------+------+------+------+------+------+------+
 * 111:104 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 * 103: 96 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 *  95: 88 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 *  87: 80 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 *  79: 72 |                          RSV                          |
 *         +------+------+------+------+------+------+------+------+
 *  71: 64 |            RSV            |           DEST            |
 *         +------+------+------+------+------+------+------+------+
 *  63: 56 |                         DEST                          |
 *         +------+------+------+------+------+------+------+------+
 *  55: 48 |                          RSV                          |
 *         +------+------+------+------+------+------+------+------+
 *  47: 40 |  RSV |         SRC_PORT          |     RSV     |TFRM_TIMER|
 *         +------+------+------+------+------+------+------+------+
 *  39: 32 |     TFRM_TIMER     |               RSV                |
 *         +------+------+------+------+------+------+------+------+
 *  31: 24 |  RSV |  DP  |   POP_CNT   |           CPUQ            |
 *         +------+------+------+------+------+------+------+------+
 *  23: 16 |           CPUQ            |      QOS_CLASS     |TAG_TYPE|
 *         +------+------+------+------+------+------+------+------+
 *  15:  8 |         PCP        |  DEI |            VID            |
 *         +------+------+------+------+------+------+------+------+
 *   7:  0 |                          VID                          |
 *         +------+------+------+------+------+------+------+------+
 *
 * And the extraction header looks like this:
 *
 *         +------+------+------+------+------+------+------+------+
 * 127:120 |  RSV |                  REW_OP                        |
 *         +------+------+------+------+------+------+------+------+
 * 119:112 |       REW_OP       |              REW_VAL             |
 *         +------+------+------+------+------+------+------+------+
 * 111:104 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 * 103: 96 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 *  95: 88 |                         REW_VAL                       |
 *         +------+------+------+------+------+------+------+------+
 *  87: 80 |       REW_VAL      |               LLEN               |
 *         +------+------+------+------+------+------+------+------+
 *  79: 72 | LLEN |                      WLEN                      |
 *         +------+------+------+------+------+------+------+------+
 *  71: 64 | WLEN |                      RSV                       |
 *         +------+------+------+------+------+------+------+------+
 *  63: 56 |                          RSV                          |
 *         +------+------+------+------+------+------+------+------+
 *  55: 48 |                          RSV                          |
 *         +------+------+------+------+------+------+------+------+
 *  47: 40 | RSV  |          SRC_PORT         |       ACL_ID       |
 *         +------+------+------+------+------+------+------+------+
 *  39: 32 |       ACL_ID       |  RSV |         SFLOW_ID          |
 *         +------+------+------+------+------+------+------+------+
 *  31: 24 |ACL_HIT| DP  |  LRN_FLAGS  |           CPUQ            |
 *         +------+------+------+------+------+------+------+------+
 *  23: 16 |           CPUQ            |      QOS_CLASS     |TAG_TYPE|
 *         +------+------+------+------+------+------+------+------+
 *  15:  8 |         PCP        |  DEI |            VID            |
 *         +------+------+------+------+------+------+------+------+
 *   7:  0 |                          VID                          |
 *         +------+------+------+------+------+------+------+------+
 */

static inline void ocelot_xfh_get_rew_val(void *extraction, u64 *rew_val)
{
	packing(extraction, rew_val, 116, 85, OCELOT_TAG_LEN, UNPACK, 0);
}

static inline void ocelot_xfh_get_len(void *extraction, u64 *len)
{
	u64 llen, wlen;

	packing(extraction, &llen, 84, 79, OCELOT_TAG_LEN, UNPACK, 0);
	packing(extraction, &wlen, 78, 71, OCELOT_TAG_LEN, UNPACK, 0);

	*len = 60 * wlen + llen - 80;
}

static inline void ocelot_xfh_get_src_port(void *extraction, u64 *src_port)
{
	packing(extraction, src_port, 46, 43, OCELOT_TAG_LEN, UNPACK, 0);
}

static inline void ocelot_xfh_get_qos_class(void *extraction, u64 *qos_class)
{
	packing(extraction, qos_class, 19, 17, OCELOT_TAG_LEN, UNPACK, 0);
}

static inline void ocelot_xfh_get_tag_type(void *extraction, u64 *tag_type)
{
	packing(extraction, tag_type, 16, 16, OCELOT_TAG_LEN, UNPACK, 0);
}

static inline void ocelot_xfh_get_vlan_tci(void *extraction, u64 *vlan_tci)
{
	packing(extraction, vlan_tci, 15, 0, OCELOT_TAG_LEN, UNPACK, 0);
}

static inline void ocelot_ifh_set_bypass(void *injection, u64 bypass)
{
	packing(injection, &bypass, 127, 127, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_rew_op(void *injection, u64 rew_op)
{
	packing(injection, &rew_op, 125, 117, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_dest(void *injection, u64 dest)
{
	packing(injection, &dest, 67, 56, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_qos_class(void *injection, u64 qos_class)
{
	packing(injection, &qos_class, 19, 17, OCELOT_TAG_LEN, PACK, 0);
}

static inline void seville_ifh_set_dest(void *injection, u64 dest)
{
	packing(injection, &dest, 67, 57, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_src(void *injection, u64 src)
{
	packing(injection, &src, 46, 43, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_tag_type(void *injection, u64 tag_type)
{
	packing(injection, &tag_type, 16, 16, OCELOT_TAG_LEN, PACK, 0);
}

static inline void ocelot_ifh_set_vid(void *injection, u64 vid)
{
	packing(injection, &vid, 11, 0, OCELOT_TAG_LEN, PACK, 0);
}

#endif
