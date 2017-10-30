/*
 * Copyright (C) 2011 Instituto Nokia de Tecnologia
 * Copyright (C) 2014 Marvell International Ltd.
 *
 * Authors:
 *    Lauro Ramos Venancio <lauro.venancio@openbossa.org>
 *    Aloisio Almeida Jr <aloisio.almeida@openbossa.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __NET_NFC_H
#define __NET_NFC_H

#include <linux/nfc.h>
#include <linux/device.h>
#include <linux/skbuff.h>

#define nfc_dbg(dev, fmt, ...) dev_dbg((dev), "NFC: " fmt, ##__VA_ARGS__)
#define nfc_info(dev, fmt, ...) dev_info((dev), "NFC: " fmt, ##__VA_ARGS__)
#define nfc_err(dev, fmt, ...) dev_err((dev), "NFC: " fmt, ##__VA_ARGS__)

struct nfc_phy_ops {
	int (*write)(void *dev_id, struct sk_buff *skb);
	int (*enable)(void *dev_id);
	void (*disable)(void *dev_id);
};

struct nfc_dev;

/**
 * data_exchange_cb_t - Definition of nfc_data_exchange callback
 *
 * @context: nfc_data_exchange cb_context parameter
 * @skb: response data
 * @err: If an error has occurred during data exchange, it is the
 *	error number. Zero means no error.
 *
 * When a rx or tx package is lost or corrupted or the target gets out
 * of the operating field, err is -EIO.
 */
typedef void (*data_exchange_cb_t)(void *context, struct sk_buff *skb,
								int err);

typedef void (*se_io_cb_t)(void *context, u8 *apdu, size_t apdu_len, int err);

struct nfc_target;

struct nfc_ops {
	int (*dev_up)(struct nfc_dev *dev);
	int (*dev_down)(struct nfc_dev *dev);
	int (*start_poll)(struct nfc_dev *dev,
			  u32 im_protocols, u32 tm_protocols);
	void (*stop_poll)(struct nfc_dev *dev);
	int (*dep_link_up)(struct nfc_dev *dev, struct nfc_target *target,
			   u8 comm_mode, u8 *gb, size_t gb_len);
	int (*dep_link_down)(struct nfc_dev *dev);
	int (*activate_target)(struct nfc_dev *dev, struct nfc_target *target,
			       u32 protocol);
	void (*deactivate_target)(struct nfc_dev *dev,
				  struct nfc_target *target, u8 mode);
	int (*im_transceive)(struct nfc_dev *dev, struct nfc_target *target,
			     struct sk_buff *skb, data_exchange_cb_t cb,
			     void *cb_context);
	int (*tm_send)(struct nfc_dev *dev, struct sk_buff *skb);
	int (*check_presence)(struct nfc_dev *dev, struct nfc_target *target);
	int (*fw_download)(struct nfc_dev *dev, const char *firmware_name);

	/* Secure Element API */
	int (*discover_se)(struct nfc_dev *dev);
	int (*enable_se)(struct nfc_dev *dev, u32 se_idx);
	int (*disable_se)(struct nfc_dev *dev, u32 se_idx);
	int (*se_io) (struct nfc_dev *dev, u32 se_idx,
		      u8 *apdu, size_t apdu_length,
		      se_io_cb_t cb, void *cb_context);
};

#define NFC_TARGET_IDX_ANY -1
#define NFC_MAX_GT_LEN 48
#define NFC_ATR_RES_GT_OFFSET 15
#define NFC_ATR_REQ_GT_OFFSET 14

/**
 * struct nfc_target - NFC target descriptiom
 *
 * @sens_res: 2 bytes describing the target SENS_RES response, if the target
 *	is a type A one. The %sens_res most significant byte must be byte 2
 *	as described by the NFC Forum digital specification (i.e. the platform
 *	configuration one) while %sens_res least significant byte is byte 1.
 */
struct nfc_target {
	u32 idx;
	u32 supported_protocols;
	u16 sens_res;
	u8 sel_res;
	u8 nfcid1_len;
	u8 nfcid1[NFC_NFCID1_MAXSIZE];
	u8 nfcid2_len;
	u8 nfcid2[NFC_NFCID2_MAXSIZE];
	u8 sensb_res_len;
	u8 sensb_res[NFC_SENSB_RES_MAXSIZE];
	u8 sensf_res_len;
	u8 sensf_res[NFC_SENSF_RES_MAXSIZE];
	u8 hci_reader_gate;
	u8 logical_idx;
	u8 is_iso15693;
	u8 iso15693_dsfid;
	u8 iso15693_uid[NFC_ISO15693_UID_MAXSIZE];
};

/**
 * nfc_se - A structure for NFC accessible secure elements.
 *
 * @idx: The secure element index. User space will enable or
 *       disable a secure element by its index.
 * @type: The secure element type. It can be SE_UICC or
 *        SE_EMBEDDED.
 * @state: The secure element state, either enabled or disabled.
 *
 */
struct nfc_se {
	struct list_head list;
	u32 idx;
	u16 type;
	u16 state;
};

/**
 * nfc_evt_transaction - A struct for NFC secure element event transaction.
 *
 * @aid: The application identifier triggering the event
 *
 * @aid_len: The application identifier length [5:16]
 *
 * @params: The application parameters transmitted during the transaction
 *
 * @params_len: The applications parameters length [0:255]
 *
 */
#define NFC_MIN_AID_LENGTH	5
#define	NFC_MAX_AID_LENGTH	16
#define NFC_MAX_PARAMS_LENGTH	255

#define NFC_EVT_TRANSACTION_AID_TAG	0x81
#define NFC_EVT_TRANSACTION_PARAMS_TAG	0x82
struct nfc_evt_transaction {
	u32 aid_len;
	u8 aid[NFC_MAX_AID_LENGTH];
	u8 params_len;
	u8 params[0];
} __packed;

struct nfc_genl_data {
	u32 poll_req_portid;
	struct mutex genl_data_mutex;
};

struct nfc_vendor_cmd {
	__u32 vendor_id;
	__u32 subcmd;
	int (*doit)(struct nfc_dev *dev, void *data, size_t data_len);
};

struct nfc_dev {
	int idx;
	u32 target_next_idx;
	struct nfc_target *targets;
	int n_targets;
	int targets_generation;
	struct device dev;
	bool dev_up;
	bool fw_download_in_progress;
	u8 rf_mode;
	bool polling;
	struct nfc_target *active_target;
	bool dep_link_up;
	struct nfc_genl_data genl_data;
	u32 supported_protocols;

	struct list_head secure_elements;

	int tx_headroom;
	int tx_tailroom;

	struct timer_list check_pres_timer;
	struct work_struct check_pres_work;

	bool shutting_down;

	struct rfkill *rfkill;

	struct nfc_vendor_cmd *vendor_cmds;
	int n_vendor_cmds;

	struct nfc_ops *ops;
	struct genl_info *cur_cmd_info;
};
#define to_nfc_dev(_dev) container_of(_dev, struct nfc_dev, dev)

extern struct class nfc_class;

struct nfc_dev *nfc_allocate_device(struct nfc_ops *ops,
				    u32 supported_protocols,
				    int tx_headroom,
				    int tx_tailroom);

/**
 * nfc_free_device - free nfc device
 *
 * @dev: The nfc device to free
 */
static inline void nfc_free_device(struct nfc_dev *dev)
{
	put_device(&dev->dev);
}

int nfc_register_device(struct nfc_dev *dev);

void nfc_unregister_device(struct nfc_dev *dev);

/**
 * nfc_set_parent_dev - set the parent device
 *
 * @nfc_dev: The nfc device whose parent is being set
 * @dev: The parent device
 */
static inline void nfc_set_parent_dev(struct nfc_dev *nfc_dev,
				      struct device *dev)
{
	nfc_dev->dev.parent = dev;
}

/**
 * nfc_set_drvdata - set driver specifc data
 *
 * @dev: The nfc device
 * @data: Pointer to driver specifc data
 */
static inline void nfc_set_drvdata(struct nfc_dev *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

/**
 * nfc_get_drvdata - get driver specifc data
 *
 * @dev: The nfc device
 */
static inline void *nfc_get_drvdata(struct nfc_dev *dev)
{
	return dev_get_drvdata(&dev->dev);
}

/**
 * nfc_device_name - get the nfc device name
 *
 * @dev: The nfc device whose name to return
 */
static inline const char *nfc_device_name(struct nfc_dev *dev)
{
	return dev_name(&dev->dev);
}

struct sk_buff *nfc_alloc_send_skb(struct nfc_dev *dev, struct sock *sk,
				   unsigned int flags, unsigned int size,
				   unsigned int *err);
struct sk_buff *nfc_alloc_recv_skb(unsigned int size, gfp_t gfp);

int nfc_set_remote_general_bytes(struct nfc_dev *dev,
				 u8 *gt, u8 gt_len);
u8 *nfc_get_local_general_bytes(struct nfc_dev *dev, size_t *gb_len);

int nfc_fw_download_done(struct nfc_dev *dev, const char *firmware_name,
			 u32 result);

int nfc_targets_found(struct nfc_dev *dev,
		      struct nfc_target *targets, int ntargets);
int nfc_target_lost(struct nfc_dev *dev, u32 target_idx);

int nfc_dep_link_is_up(struct nfc_dev *dev, u32 target_idx,
		       u8 comm_mode, u8 rf_mode);

int nfc_tm_activated(struct nfc_dev *dev, u32 protocol, u8 comm_mode,
		     u8 *gb, size_t gb_len);
int nfc_tm_deactivated(struct nfc_dev *dev);
int nfc_tm_data_received(struct nfc_dev *dev, struct sk_buff *skb);

void nfc_driver_failure(struct nfc_dev *dev, int err);

int nfc_se_transaction(struct nfc_dev *dev, u8 se_idx,
		       struct nfc_evt_transaction *evt_transaction);
int nfc_se_connectivity(struct nfc_dev *dev, u8 se_idx);
int nfc_add_se(struct nfc_dev *dev, u32 se_idx, u16 type);
int nfc_remove_se(struct nfc_dev *dev, u32 se_idx);
struct nfc_se *nfc_find_se(struct nfc_dev *dev, u32 se_idx);

void nfc_send_to_raw_sock(struct nfc_dev *dev, struct sk_buff *skb,
			  u8 payload_type, u8 direction);

static inline int nfc_set_vendor_cmds(struct nfc_dev *dev,
				      struct nfc_vendor_cmd *cmds,
				      int n_cmds)
{
	if (dev->vendor_cmds || dev->n_vendor_cmds)
		return -EINVAL;

	dev->vendor_cmds = cmds;
	dev->n_vendor_cmds = n_cmds;

	return 0;
}

struct sk_buff *__nfc_alloc_vendor_cmd_reply_skb(struct nfc_dev *dev,
						 enum nfc_attrs attr,
						 u32 oui, u32 subcmd,
						 int approxlen);
int nfc_vendor_cmd_reply(struct sk_buff *skb);

/**
 * nfc_vendor_cmd_alloc_reply_skb - allocate vendor command reply
 * @dev: nfc device
 * @oui: vendor oui
 * @approxlen: an upper bound of the length of the data that will
 *      be put into the skb
 *
 * This function allocates and pre-fills an skb for a reply to
 * a vendor command. Since it is intended for a reply, calling
 * it outside of a vendor command's doit() operation is invalid.
 *
 * The returned skb is pre-filled with some identifying data in
 * a way that any data that is put into the skb (with skb_put(),
 * nla_put() or similar) will end up being within the
 * %NFC_ATTR_VENDOR_DATA attribute, so all that needs to be done
 * with the skb is adding data for the corresponding userspace tool
 * which can then read that data out of the vendor data attribute.
 * You must not modify the skb in any other way.
 *
 * When done, call nfc_vendor_cmd_reply() with the skb and return
 * its error code as the result of the doit() operation.
 *
 * Return: An allocated and pre-filled skb. %NULL if any errors happen.
 */
static inline struct sk_buff *
nfc_vendor_cmd_alloc_reply_skb(struct nfc_dev *dev,
				u32 oui, u32 subcmd, int approxlen)
{
	return __nfc_alloc_vendor_cmd_reply_skb(dev,
						NFC_ATTR_VENDOR_DATA,
						oui,
						subcmd, approxlen);
}

#endif /* __NET_NFC_H */
