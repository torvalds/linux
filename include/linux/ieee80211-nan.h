/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WFA NAN definitions
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005, Devicescape Software, Inc.
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (c) 2018 - 2025 Intel Corporation
 */

#ifndef LINUX_IEEE80211_NAN_H
#define LINUX_IEEE80211_NAN_H

/* NAN operation mode, as defined in Wi-Fi Aware (TM) specification Table 81 */
#define NAN_OP_MODE_PHY_MODE_VHT	0x01
#define NAN_OP_MODE_PHY_MODE_HE		0x10
#define NAN_OP_MODE_PHY_MODE_MASK	0x11
#define NAN_OP_MODE_80P80MHZ		0x02
#define NAN_OP_MODE_160MHZ		0x04
#define NAN_OP_MODE_PNDL_SUPPRTED	0x08

/* NAN Device capabilities, as defined in Wi-Fi Aware (TM) specification
 * Table 79
 */
#define NAN_DEV_CAPA_DFS_OWNER			0x01
#define NAN_DEV_CAPA_EXT_KEY_ID_SUPPORTED	0x02
#define NAN_DEV_CAPA_SIM_NDP_RX_SUPPORTED	0x04
#define NAN_DEV_CAPA_NDPE_SUPPORTED		0x08
#define NAN_DEV_CAPA_S3_SUPPORTED		0x10

#endif /* LINUX_IEEE80211_NAN_H */
