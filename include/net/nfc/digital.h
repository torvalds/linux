/*
 * NFC Digital Protocol stack
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __NFC_DIGITAL_H
#define __NFC_DIGITAL_H

#include <linux/skbuff.h>
#include <net/nfc/nfc.h>

/**
 * Configuration types for in_configure_hw and tg_configure_hw.
 */
enum {
	NFC_DIGITAL_CONFIG_RF_TECH = 0,
	NFC_DIGITAL_CONFIG_FRAMING,
};

/**
 * RF technology values passed as param argument to in_configure_hw and
 * tg_configure_hw for NFC_DIGITAL_CONFIG_RF_TECH configuration type.
 */
enum {
	NFC_DIGITAL_RF_TECH_106A = 0,
	NFC_DIGITAL_RF_TECH_212F,
	NFC_DIGITAL_RF_TECH_424F,

	NFC_DIGITAL_RF_TECH_LAST,
};

/**
 * Framing configuration passed as param argument to in_configure_hw and
 * tg_configure_hw for NFC_DIGITAL_CONFIG_FRAMING configuration type.
 */
enum {
	NFC_DIGITAL_FRAMING_NFCA_SHORT = 0,
	NFC_DIGITAL_FRAMING_NFCA_STANDARD,
	NFC_DIGITAL_FRAMING_NFCA_STANDARD_WITH_CRC_A,

	NFC_DIGITAL_FRAMING_NFCA_T1T,
	NFC_DIGITAL_FRAMING_NFCA_T2T,
	NFC_DIGITAL_FRAMING_NFCA_NFC_DEP,

	NFC_DIGITAL_FRAMING_NFCF,
	NFC_DIGITAL_FRAMING_NFCF_T3T,
	NFC_DIGITAL_FRAMING_NFCF_NFC_DEP,
	NFC_DIGITAL_FRAMING_NFC_DEP_ACTIVATED,

	NFC_DIGITAL_FRAMING_LAST,
};

#define DIGITAL_MDAA_NFCID1_SIZE 3

struct digital_tg_mdaa_params {
	u16 sens_res;
	u8 nfcid1[DIGITAL_MDAA_NFCID1_SIZE];
	u8 sel_res;

	u8 nfcid2[NFC_NFCID2_MAXSIZE];
	u16 sc;
};

struct nfc_digital_dev;

/**
 * nfc_digital_cmd_complete_t - Definition of command result callback
 *
 * @ddev: nfc_digital_device ref
 * @arg: user data
 * @resp: response data
 *
 * resp pointer can be an error code and will be checked with IS_ERR() macro.
 * The callback is responsible for freeing resp sk_buff.
 */
typedef void (*nfc_digital_cmd_complete_t)(struct nfc_digital_dev *ddev,
					   void *arg, struct sk_buff *resp);

/**
 * Device side NFC Digital operations
 *
 * Initiator mode:
 * @in_configure_hw: Hardware configuration for RF technology and communication
 *	framing in initiator mode. This is a synchronous function.
 * @in_send_cmd: Initiator mode data exchange using RF technology and framing
 *	previously set with in_configure_hw. The peer response is returned
 *	through callback cb. If an io error occurs or the peer didn't reply
 *	within the specified timeout (ms), the error code is passed back through
 *	the resp pointer. This is an asynchronous function.
 *
 * Target mode: Only NFC-DEP protocol is supported in target mode.
 * @tg_configure_hw: Hardware configuration for RF technology and communication
 *	framing in target mode. This is a synchronous function.
 * @tg_send_cmd: Target mode data exchange using RF technology and framing
 *	previously set with tg_configure_hw. The peer next command is returned
 *	through callback cb. If an io error occurs or the peer didn't reply
 *	within the specified timeout (ms), the error code is passed back through
 *	the resp pointer. This is an asynchronous function.
 * @tg_listen: Put the device in listen mode waiting for data from the peer
 *	device. This is an asynchronous function.
 * @tg_listen_mdaa: If supported, put the device in automatic listen mode with
 *	mode detection and automatic anti-collision. In this mode, the device
 *	automatically detects the RF technology and executes the anti-collision
 *	detection using the command responses specified in mdaa_params. The
 *	mdaa_params structure contains SENS_RES, NFCID1, and SEL_RES for 106A RF
 *	tech. NFCID2 and system code (sc) for 212F and 424F. The driver returns
 *	the NFC-DEP ATR_REQ command through cb. The digital stack deducts the RF
 *	tech by analyzing the SoD of the frame containing the ATR_REQ command.
 *	This is an asynchronous function.
 *
 * @switch_rf: Turns device radio on or off. The stack does not call explicitly
 *	switch_rf to turn the radio on. A call to in|tg_configure_hw must turn
 *	the device radio on.
 * @abort_cmd: Discard the last sent command.
 */
struct nfc_digital_ops {
	int (*in_configure_hw)(struct nfc_digital_dev *ddev, int type,
			       int param);
	int (*in_send_cmd)(struct nfc_digital_dev *ddev, struct sk_buff *skb,
			   u16 timeout, nfc_digital_cmd_complete_t cb,
			   void *arg);

	int (*tg_configure_hw)(struct nfc_digital_dev *ddev, int type,
			       int param);
	int (*tg_send_cmd)(struct nfc_digital_dev *ddev, struct sk_buff *skb,
			   u16 timeout, nfc_digital_cmd_complete_t cb,
			   void *arg);
	int (*tg_listen)(struct nfc_digital_dev *ddev, u16 timeout,
			 nfc_digital_cmd_complete_t cb, void *arg);
	int (*tg_listen_mdaa)(struct nfc_digital_dev *ddev,
			      struct digital_tg_mdaa_params *mdaa_params,
			      u16 timeout, nfc_digital_cmd_complete_t cb,
			      void *arg);

	int (*switch_rf)(struct nfc_digital_dev *ddev, bool on);
	void (*abort_cmd)(struct nfc_digital_dev *ddev);
};

/**
 * Driver capabilities - bit mask made of the following values
 *
 * @NFC_DIGITAL_DRV_CAPS_IN_CRC: The driver handles CRC calculation in initiator
 *	mode.
 * @NFC_DIGITAL_DRV_CAPS_TG_CRC: The driver handles CRC calculation in target
 *	mode.
 */
#define NFC_DIGITAL_DRV_CAPS_IN_CRC	0x0001
#define NFC_DIGITAL_DRV_CAPS_TG_CRC	0x0002

struct nfc_digital_dev {
	struct nfc_dev *nfc_dev;
	struct nfc_digital_ops *ops;

	u32 protocols;

	int tx_headroom;
	int tx_tailroom;

	u32 driver_capabilities;
	void *driver_data;
};

struct nfc_digital_dev *nfc_digital_allocate_device(struct nfc_digital_ops *ops,
						    __u32 supported_protocols,
						    __u32 driver_capabilities,
						    int tx_headroom,
						    int tx_tailroom);
void nfc_digital_free_device(struct nfc_digital_dev *ndev);
int nfc_digital_register_device(struct nfc_digital_dev *ndev);
void nfc_digital_unregister_device(struct nfc_digital_dev *ndev);

static inline void nfc_digital_set_parent_dev(struct nfc_digital_dev *ndev,
					      struct device *dev)
{
	nfc_set_parent_dev(ndev->nfc_dev, dev);
}

static inline void nfc_digital_set_drvdata(struct nfc_digital_dev *dev,
					   void *data)
{
	dev->driver_data = data;
}

static inline void *nfc_digital_get_drvdata(struct nfc_digital_dev *dev)
{
	return dev->driver_data;
}

#endif /* __NFC_DIGITAL_H */
