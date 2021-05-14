/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *  Copyright (C) 2013 Intel Corporation. All rights reserved.
 *  Copyright (C) 2014 Marvell International Ltd.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_core.h, which was written
 *  by Maxim Krasnyansky.
 */

#ifndef __NCI_CORE_H
#define __NCI_CORE_H

#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/tty.h>

#include <net/nfc/nfc.h>
#include <net/nfc/nci.h>

/* NCI device flags */
enum nci_flag {
	NCI_INIT,
	NCI_UP,
	NCI_DATA_EXCHANGE,
	NCI_DATA_EXCHANGE_TO,
};

/* NCI device states */
enum nci_state {
	NCI_IDLE,
	NCI_DISCOVERY,
	NCI_W4_ALL_DISCOVERIES,
	NCI_W4_HOST_SELECT,
	NCI_POLL_ACTIVE,
	NCI_LISTEN_ACTIVE,
	NCI_LISTEN_SLEEP,
};

/* NCI timeouts */
#define NCI_RESET_TIMEOUT			5000
#define NCI_INIT_TIMEOUT			5000
#define NCI_SET_CONFIG_TIMEOUT			5000
#define NCI_RF_DISC_TIMEOUT			5000
#define NCI_RF_DISC_SELECT_TIMEOUT		5000
#define NCI_RF_DEACTIVATE_TIMEOUT		30000
#define NCI_CMD_TIMEOUT				5000
#define NCI_DATA_TIMEOUT			700

struct nci_dev;

struct nci_driver_ops {
	__u16 opcode;
	int (*rsp)(struct nci_dev *dev, struct sk_buff *skb);
	int (*ntf)(struct nci_dev *dev, struct sk_buff *skb);
};

struct nci_ops {
	int   (*init)(struct nci_dev *ndev);
	int   (*open)(struct nci_dev *ndev);
	int   (*close)(struct nci_dev *ndev);
	int   (*send)(struct nci_dev *ndev, struct sk_buff *skb);
	int   (*setup)(struct nci_dev *ndev);
	int   (*post_setup)(struct nci_dev *ndev);
	int   (*fw_download)(struct nci_dev *ndev, const char *firmware_name);
	__u32 (*get_rfprotocol)(struct nci_dev *ndev, __u8 rf_protocol);
	int   (*discover_se)(struct nci_dev *ndev);
	int   (*disable_se)(struct nci_dev *ndev, u32 se_idx);
	int   (*enable_se)(struct nci_dev *ndev, u32 se_idx);
	int   (*se_io)(struct nci_dev *ndev, u32 se_idx,
				u8 *apdu, size_t apdu_length,
				se_io_cb_t cb, void *cb_context);
	int   (*hci_load_session)(struct nci_dev *ndev);
	void  (*hci_event_received)(struct nci_dev *ndev, u8 pipe, u8 event,
				    struct sk_buff *skb);
	void  (*hci_cmd_received)(struct nci_dev *ndev, u8 pipe, u8 cmd,
				  struct sk_buff *skb);

	struct nci_driver_ops *prop_ops;
	size_t n_prop_ops;

	struct nci_driver_ops *core_ops;
	size_t n_core_ops;
};

#define NCI_MAX_SUPPORTED_RF_INTERFACES		4
#define NCI_MAX_DISCOVERED_TARGETS		10
#define NCI_MAX_NUM_NFCEE   255
#define NCI_MAX_CONN_ID		7
#define NCI_MAX_PROPRIETARY_CMD 64

struct nci_conn_info {
	struct list_head list;
	/* NCI specification 4.4.2 Connection Creation
	 * The combination of destination type and destination specific
	 * parameters shall uniquely identify a single destination for the
	 * Logical Connection
	 */
	struct dest_spec_params *dest_params;
	__u8	dest_type;
	__u8	conn_id;
	__u8	max_pkt_payload_len;

	atomic_t credits_cnt;
	__u8	 initial_num_credits;

	data_exchange_cb_t	data_exchange_cb;
	void *data_exchange_cb_context;

	struct sk_buff *rx_skb;
};

#define NCI_INVALID_CONN_ID 0x80

#define NCI_HCI_ANY_OPEN_PIPE      0x03

/* Gates */
#define NCI_HCI_ADMIN_GATE         0x00
#define NCI_HCI_LOOPBACK_GATE	   0x04
#define NCI_HCI_IDENTITY_MGMT_GATE 0x05
#define NCI_HCI_LINK_MGMT_GATE     0x06

/* Pipes */
#define NCI_HCI_LINK_MGMT_PIPE             0x00
#define NCI_HCI_ADMIN_PIPE                 0x01

/* Generic responses */
#define NCI_HCI_ANY_OK                     0x00
#define NCI_HCI_ANY_E_NOT_CONNECTED        0x01
#define NCI_HCI_ANY_E_CMD_PAR_UNKNOWN      0x02
#define NCI_HCI_ANY_E_NOK                  0x03
#define NCI_HCI_ANY_E_PIPES_FULL           0x04
#define NCI_HCI_ANY_E_REG_PAR_UNKNOWN      0x05
#define NCI_HCI_ANY_E_PIPE_NOT_OPENED      0x06
#define NCI_HCI_ANY_E_CMD_NOT_SUPPORTED    0x07
#define NCI_HCI_ANY_E_INHIBITED            0x08
#define NCI_HCI_ANY_E_TIMEOUT              0x09
#define NCI_HCI_ANY_E_REG_ACCESS_DENIED    0x0a
#define NCI_HCI_ANY_E_PIPE_ACCESS_DENIED   0x0b

#define NCI_HCI_DO_NOT_OPEN_PIPE           0x81
#define NCI_HCI_INVALID_PIPE               0x80
#define NCI_HCI_INVALID_GATE               0xFF
#define NCI_HCI_INVALID_HOST               0x80

#define NCI_HCI_MAX_CUSTOM_GATES   50
/*
 * According to specification 102 622 chapter 4.4 Pipes,
 * the pipe identifier is 7 bits long.
 */
#define NCI_HCI_MAX_PIPES          128

struct nci_hci_gate {
	u8 gate;
	u8 pipe;
	u8 dest_host;
} __packed;

struct nci_hci_pipe {
	u8 gate;
	u8 host;
} __packed;

struct nci_hci_init_data {
	u8 gate_count;
	struct nci_hci_gate gates[NCI_HCI_MAX_CUSTOM_GATES];
	char session_id[9];
};

#define NCI_HCI_MAX_GATES          256

struct nci_hci_dev {
	u8 nfcee_id;
	struct nci_dev *ndev;
	struct nci_conn_info *conn_info;

	struct nci_hci_init_data init_data;
	struct nci_hci_pipe pipes[NCI_HCI_MAX_PIPES];
	u8 gate2pipe[NCI_HCI_MAX_GATES];
	int expected_pipes;
	int count_pipes;

	struct sk_buff_head rx_hcp_frags;
	struct work_struct msg_rx_work;
	struct sk_buff_head msg_rx_queue;
};

/* NCI Core structures */
struct nci_dev {
	struct nfc_dev		*nfc_dev;
	struct nci_ops		*ops;
	struct nci_hci_dev	*hci_dev;

	int			tx_headroom;
	int			tx_tailroom;

	atomic_t		state;
	unsigned long		flags;

	atomic_t		cmd_cnt;
	__u8			cur_conn_id;

	struct list_head	conn_info_list;
	struct nci_conn_info	*rf_conn_info;

	struct timer_list	cmd_timer;
	struct timer_list	data_timer;

	struct workqueue_struct	*cmd_wq;
	struct work_struct	cmd_work;

	struct workqueue_struct	*rx_wq;
	struct work_struct	rx_work;

	struct workqueue_struct	*tx_wq;
	struct work_struct	tx_work;

	struct sk_buff_head	cmd_q;
	struct sk_buff_head	rx_q;
	struct sk_buff_head	tx_q;

	struct mutex		req_lock;
	struct completion	req_completion;
	__u32			req_status;
	__u32			req_result;

	void			*driver_data;

	__u32			poll_prots;
	__u32			target_active_prot;

	struct nfc_target	targets[NCI_MAX_DISCOVERED_TARGETS];
	int			n_targets;

	/* received during NCI_OP_CORE_RESET_RSP */
	__u8			nci_ver;

	/* received during NCI_OP_CORE_INIT_RSP */
	__u32			nfcc_features;
	__u8			num_supported_rf_interfaces;
	__u8			supported_rf_interfaces
				[NCI_MAX_SUPPORTED_RF_INTERFACES];
	__u8			max_logical_connections;
	__u16			max_routing_table_size;
	__u8			max_ctrl_pkt_payload_len;
	__u16			max_size_for_large_params;
	__u8			manufact_id;
	__u32			manufact_specific_info;

	/* Save RF Discovery ID or NFCEE ID under conn_create */
	struct dest_spec_params cur_params;
	/* Save destination type under conn_create */
	__u8			cur_dest_type;

	/* stored during nci_data_exchange */
	struct sk_buff		*rx_data_reassembly;

	/* stored during intf_activated_ntf */
	__u8 remote_gb[NFC_MAX_GT_LEN];
	__u8 remote_gb_len;
};

/* ----- NCI Devices ----- */
struct nci_dev *nci_allocate_device(struct nci_ops *ops,
				    __u32 supported_protocols,
				    int tx_headroom,
				    int tx_tailroom);
void nci_free_device(struct nci_dev *ndev);
int nci_register_device(struct nci_dev *ndev);
void nci_unregister_device(struct nci_dev *ndev);
int nci_request(struct nci_dev *ndev,
		void (*req)(struct nci_dev *ndev,
			    unsigned long opt),
		unsigned long opt, __u32 timeout);
int nci_prop_cmd(struct nci_dev *ndev, __u8 oid, size_t len, __u8 *payload);
int nci_core_cmd(struct nci_dev *ndev, __u16 opcode, size_t len, __u8 *payload);
int nci_core_reset(struct nci_dev *ndev);
int nci_core_init(struct nci_dev *ndev);

int nci_recv_frame(struct nci_dev *ndev, struct sk_buff *skb);
int nci_send_frame(struct nci_dev *ndev, struct sk_buff *skb);
int nci_set_config(struct nci_dev *ndev, __u8 id, size_t len, __u8 *val);

int nci_nfcee_discover(struct nci_dev *ndev, u8 action);
int nci_nfcee_mode_set(struct nci_dev *ndev, u8 nfcee_id, u8 nfcee_mode);
int nci_core_conn_create(struct nci_dev *ndev, u8 destination_type,
			 u8 number_destination_params,
			 size_t params_len,
			 struct core_conn_create_dest_spec_params *params);
int nci_core_conn_close(struct nci_dev *ndev, u8 conn_id);
int nci_nfcc_loopback(struct nci_dev *ndev, void *data, size_t data_len,
		      struct sk_buff **resp);

struct nci_hci_dev *nci_hci_allocate(struct nci_dev *ndev);
void nci_hci_deallocate(struct nci_dev *ndev);
int nci_hci_send_event(struct nci_dev *ndev, u8 gate, u8 event,
		       const u8 *param, size_t param_len);
int nci_hci_send_cmd(struct nci_dev *ndev, u8 gate,
		     u8 cmd, const u8 *param, size_t param_len,
		     struct sk_buff **skb);
int nci_hci_open_pipe(struct nci_dev *ndev, u8 pipe);
int nci_hci_connect_gate(struct nci_dev *ndev, u8 dest_host,
			 u8 dest_gate, u8 pipe);
int nci_hci_set_param(struct nci_dev *ndev, u8 gate, u8 idx,
		      const u8 *param, size_t param_len);
int nci_hci_get_param(struct nci_dev *ndev, u8 gate, u8 idx,
		      struct sk_buff **skb);
int nci_hci_clear_all_pipes(struct nci_dev *ndev);
int nci_hci_dev_session_init(struct nci_dev *ndev);

static inline struct sk_buff *nci_skb_alloc(struct nci_dev *ndev,
					    unsigned int len,
					    gfp_t how)
{
	struct sk_buff *skb;

	skb = alloc_skb(len + ndev->tx_headroom + ndev->tx_tailroom, how);
	if (skb)
		skb_reserve(skb, ndev->tx_headroom);

	return skb;
}

static inline void nci_set_parent_dev(struct nci_dev *ndev, struct device *dev)
{
	nfc_set_parent_dev(ndev->nfc_dev, dev);
}

static inline void nci_set_drvdata(struct nci_dev *ndev, void *data)
{
	ndev->driver_data = data;
}

static inline void *nci_get_drvdata(struct nci_dev *ndev)
{
	return ndev->driver_data;
}

static inline int nci_set_vendor_cmds(struct nci_dev *ndev,
				      struct nfc_vendor_cmd *cmds,
				      int n_cmds)
{
	return nfc_set_vendor_cmds(ndev->nfc_dev, cmds, n_cmds);
}

void nci_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb);
void nci_ntf_packet(struct nci_dev *ndev, struct sk_buff *skb);
int nci_prop_rsp_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb);
int nci_prop_ntf_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb);
int nci_core_rsp_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb);
int nci_core_ntf_packet(struct nci_dev *ndev, __u16 opcode,
			struct sk_buff *skb);
void nci_rx_data_packet(struct nci_dev *ndev, struct sk_buff *skb);
int nci_send_cmd(struct nci_dev *ndev, __u16 opcode, __u8 plen, void *payload);
int nci_send_data(struct nci_dev *ndev, __u8 conn_id, struct sk_buff *skb);
int nci_conn_max_data_pkt_payload_size(struct nci_dev *ndev, __u8 conn_id);
void nci_data_exchange_complete(struct nci_dev *ndev, struct sk_buff *skb,
				__u8 conn_id, int err);
void nci_hci_data_received_cb(void *context, struct sk_buff *skb, int err);

void nci_clear_target_list(struct nci_dev *ndev);

/* ----- NCI requests ----- */
#define NCI_REQ_DONE		0
#define NCI_REQ_PEND		1
#define NCI_REQ_CANCELED	2

void nci_req_complete(struct nci_dev *ndev, int result);
struct nci_conn_info *nci_get_conn_info_by_conn_id(struct nci_dev *ndev,
						   int conn_id);
int nci_get_conn_info_by_dest_type_params(struct nci_dev *ndev, u8 dest_type,
					  struct dest_spec_params *params);

/* ----- NCI status code ----- */
int nci_to_errno(__u8 code);

/* ----- NCI over SPI acknowledge modes ----- */
#define NCI_SPI_CRC_DISABLED	0x00
#define NCI_SPI_CRC_ENABLED	0x01

/* ----- NCI SPI structures ----- */
struct nci_spi {
	struct nci_dev		*ndev;
	struct spi_device	*spi;

	unsigned int		xfer_udelay;	/* microseconds delay between
						  transactions */

	unsigned int		xfer_speed_hz; /*
						* SPI clock frequency
						* 0 => default clock
						*/

	u8			acknowledge_mode;

	struct completion	req_completion;
	u8			req_result;
};

/* ----- NCI SPI ----- */
struct nci_spi *nci_spi_allocate_spi(struct spi_device *spi,
				     u8 acknowledge_mode, unsigned int delay,
				     struct nci_dev *ndev);
int nci_spi_send(struct nci_spi *nspi,
		 struct completion *write_handshake_completion,
		 struct sk_buff *skb);
struct sk_buff *nci_spi_read(struct nci_spi *nspi);

/* ----- NCI UART ---- */

/* Ioctl */
#define NCIUARTSETDRIVER	_IOW('U', 0, char *)

enum nci_uart_driver {
	NCI_UART_DRIVER_MARVELL = 0,
	NCI_UART_DRIVER_MAX
};

struct nci_uart;

struct nci_uart_ops {
	int (*open)(struct nci_uart *nci_uart);
	void (*close)(struct nci_uart *nci_uart);
	int (*recv)(struct nci_uart *nci_uart, struct sk_buff *skb);
	int (*recv_buf)(struct nci_uart *nci_uart, const u8 *data, char *flags,
			int count);
	int (*send)(struct nci_uart *nci_uart, struct sk_buff *skb);
	void (*tx_start)(struct nci_uart *nci_uart);
	void (*tx_done)(struct nci_uart *nci_uart);
};

struct nci_uart {
	struct module		*owner;
	struct nci_uart_ops	ops;
	const char		*name;
	enum nci_uart_driver	driver;

	/* Dynamic data */
	struct nci_dev		*ndev;
	spinlock_t		rx_lock;
	struct work_struct	write_work;
	struct tty_struct	*tty;
	unsigned long		tx_state;
	struct sk_buff_head	tx_q;
	struct sk_buff		*tx_skb;
	struct sk_buff		*rx_skb;
	int			rx_packet_len;
	void			*drv_data;
};

int nci_uart_register(struct nci_uart *nu);
void nci_uart_unregister(struct nci_uart *nu);
void nci_uart_set_config(struct nci_uart *nu, int baudrate, int flow_ctrl);

#endif /* __NCI_CORE_H */
