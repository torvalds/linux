/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 sigma star gmbh
 *
 * Specifies paes key slot handles for NXP's DCP (Data Co-Processor) to be used
 * with the crypto_skcipher_setkey().
 */

#ifndef MXS_DCP_H
#define MXS_DCP_H

#define DCP_PAES_KEYSIZE 1
#define DCP_PAES_KEY_SLOT0 0x00
#define DCP_PAES_KEY_SLOT1 0x01
#define DCP_PAES_KEY_SLOT2 0x02
#define DCP_PAES_KEY_SLOT3 0x03
#define DCP_PAES_KEY_UNIQUE 0xfe
#define DCP_PAES_KEY_OTP 0xff

#endif /* MXS_DCP_H */
