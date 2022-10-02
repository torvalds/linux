/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ppp_defs.h - PPP definitions.
 *
 * Copyright 1994-2000 Paul Mackerras.
 */
#ifndef _PPP_DEFS_H_
#define _PPP_DEFS_H_

#include <linux/crc-ccitt.h>
#include <uapi/linux/ppp_defs.h>

#define PPP_FCS(fcs, c) crc_ccitt_byte(fcs, c)

/**
 * ppp_proto_is_valid - checks if PPP protocol is valid
 * @proto: PPP protocol
 *
 * Assumes proto is not compressed.
 * Protocol is valid if the value is odd and the least significant bit of the
 * most significant octet is 0 (see RFC 1661, section 2).
 */
static inline bool ppp_proto_is_valid(u16 proto)
{
	return !!((proto & 0x0101) == 0x0001);
}

#endif /* _PPP_DEFS_H_ */
