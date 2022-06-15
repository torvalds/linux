/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */


#ifndef _UAPI_MSM_RMNET_H_
#define _UAPI_MSM_RMNET_H_

#include <linux/types.h>

/* Bitmap macros for RmNET driver operation mode. */
#define RMNET_MODE_NONE     (0x00)
#define RMNET_MODE_LLP_ETH  (0x01)
#define RMNET_MODE_LLP_IP   (0x02)
#define RMNET_MODE_QOS      (0x04)
#define RMNET_MODE_MASK     (RMNET_MODE_LLP_ETH | \
			     RMNET_MODE_LLP_IP  | \
			     RMNET_MODE_QOS)

#define RMNET_IS_MODE_QOS(mode)  \
	((mode & RMNET_MODE_QOS) == RMNET_MODE_QOS)
#define RMNET_IS_MODE_IP(mode)   \
	((mode & RMNET_MODE_LLP_IP) == RMNET_MODE_LLP_IP)

/**
 * IOCTL commands
 * Values chosen to not conflict with other drivers in the ecosystem
 */

#define RMNET_IOCTL_SET_LLP_ETHERNET 0x000089F1 /* Set Ethernet protocol  */
#define RMNET_IOCTL_SET_LLP_IP       0x000089F2 /* Set RAWIP protocol     */
#define RMNET_IOCTL_GET_LLP          0x000089F3 /* Get link protocol      */
#define RMNET_IOCTL_SET_QOS_ENABLE   0x000089F4 /* Set QoS header enabled */
#define RMNET_IOCTL_SET_QOS_DISABLE  0x000089F5 /* Set QoS header disabled*/
#define RMNET_IOCTL_GET_QOS          0x000089F6 /* Get QoS header state   */
#define RMNET_IOCTL_GET_OPMODE       0x000089F7 /* Get operation mode     */
#define RMNET_IOCTL_OPEN             0x000089F8 /* Open transport port    */
#define RMNET_IOCTL_CLOSE            0x000089F9 /* Close transport port   */
#define RMNET_IOCTL_FLOW_ENABLE      0x000089FA /* Flow enable            */
#define RMNET_IOCTL_FLOW_DISABLE     0x000089FB /* Flow disable           */
#define RMNET_IOCTL_FLOW_SET_HNDL    0x000089FC /* Set flow handle        */
#define RMNET_IOCTL_EXTENDED         0x000089FD /* Extended IOCTLs        */
#define RMNET_IOCTL_EXTENDED_V2      0x000089FE /* Extended V2 IOCTLs     */

/* RmNet Data Required IOCTLs */
#define RMNET_IOCTL_GET_SUPPORTED_FEATURES     0x0000   /* Get features    */
#define RMNET_IOCTL_SET_MRU                    0x0001   /* Set MRU         */
#define RMNET_IOCTL_GET_MRU                    0x0002   /* Get MRU         */
#define RMNET_IOCTL_GET_EPID                   0x0003   /* Get endpoint ID */
#define RMNET_IOCTL_GET_DRIVER_NAME            0x0004   /* Get driver name */
#define RMNET_IOCTL_ADD_MUX_CHANNEL            0x0005   /* Add MUX ID      */
#define RMNET_IOCTL_SET_EGRESS_DATA_FORMAT     0x0006   /* Set EDF         */
#define RMNET_IOCTL_SET_INGRESS_DATA_FORMAT    0x0007   /* Set IDF         */
#define RMNET_IOCTL_SET_AGGREGATION_COUNT      0x0008   /* Set agg count   */
#define RMNET_IOCTL_GET_AGGREGATION_COUNT      0x0009   /* Get agg count   */
#define RMNET_IOCTL_SET_AGGREGATION_SIZE       0x000A   /* Set agg size    */
#define RMNET_IOCTL_GET_AGGREGATION_SIZE       0x000B   /* Get agg size    */
#define RMNET_IOCTL_FLOW_CONTROL               0x000C   /* Do flow control */
#define RMNET_IOCTL_GET_DFLT_CONTROL_CHANNEL   0x000D   /* For legacy use  */
#define RMNET_IOCTL_GET_HWSW_MAP               0x000E   /* Get HW/SW map   */
#define RMNET_IOCTL_SET_RX_HEADROOM            0x000F   /* RX Headroom     */
#define RMNET_IOCTL_GET_EP_PAIR                0x0010   /* Endpoint pair   */
#define RMNET_IOCTL_SET_QOS_VERSION            0x0011   /* 8/6 byte QoS hdr*/
#define RMNET_IOCTL_GET_QOS_VERSION            0x0012   /* 8/6 byte QoS hdr*/
#define RMNET_IOCTL_GET_SUPPORTED_QOS_MODES    0x0013   /* Get QoS modes   */
#define RMNET_IOCTL_SET_SLEEP_STATE            0x0014   /* Set sleep state */
#define RMNET_IOCTL_SET_XLAT_DEV_INFO          0x0015   /* xlat dev name   */
#define RMNET_IOCTL_DEREGISTER_DEV             0x0016   /* Dereg a net dev */
#define RMNET_IOCTL_GET_SG_SUPPORT             0x0017   /* Query sg support*/
#define RMNET_IOCTL_SET_OFFLOAD                0x0018   /* Set IPA offload */
#define RMNET_IOCTL_GET_MTU                    0x0019   /* Get v4/v6 MTU   */
#define RMNET_IOCTL_SET_MTU                    0x0020   /* Set v4/v6 MTU   */
#define RMNET_IOCTL_GET_EPID_LL                0x0021   /* Get LL ep ID    */
#define RMNET_IOCTL_GET_EP_PAIR_LL             0x0022   /* LL ep pair      */

/**
 * RMNET_IOCTL_EXTENDED_V2 ioctl types.
 * Should be sent through "extended_ioctl_type" variable.
 * Any number of new IOCTL type can be added.
 */
/**
 * Set EDF with config values
 * Includes all the egress pipe's config in one single ioctl
 */
#define RMNET_IOCTL_SET_EGRESS_DATA_FORMAT_V2	0x0000
/**
 * Set IDF with config values
 * Includes all the ingress pipe's config in one single ioctl
 */
#define RMNET_IOCTL_SET_INGRESS_DATA_FORMAT_V2	0x0001

/* Return values for the RMNET_IOCTL_GET_SUPPORTED_FEATURES IOCTL */
#define RMNET_IOCTL_FEAT_NOTIFY_MUX_CHANNEL              (1<<0)
#define RMNET_IOCTL_FEAT_SET_EGRESS_DATA_FORMAT          (1<<1)
#define RMNET_IOCTL_FEAT_SET_INGRESS_DATA_FORMAT         (1<<2)
#define RMNET_IOCTL_FEAT_SET_AGGREGATION_COUNT           (1<<3)
#define RMNET_IOCTL_FEAT_GET_AGGREGATION_COUNT           (1<<4)
#define RMNET_IOCTL_FEAT_SET_AGGREGATION_SIZE            (1<<5)
#define RMNET_IOCTL_FEAT_GET_AGGREGATION_SIZE            (1<<6)
#define RMNET_IOCTL_FEAT_FLOW_CONTROL                    (1<<7)
#define RMNET_IOCTL_FEAT_GET_DFLT_CONTROL_CHANNEL        (1<<8)
#define RMNET_IOCTL_FEAT_GET_HWSW_MAP                    (1<<9)

/* Input values for the RMNET_IOCTL_SET_EGRESS_DATA_FORMAT IOCTL  */
#define RMNET_IOCTL_EGRESS_FORMAT_MAP                  (1<<1)
#define RMNET_IOCTL_EGRESS_FORMAT_AGGREGATION          (1<<2)
#define RMNET_IOCTL_EGRESS_FORMAT_MUXING               (1<<3)
#define RMNET_IOCTL_EGRESS_FORMAT_CHECKSUM             (1<<4)

/* Input values for the RMNET_IOCTL_SET_INGRESS_DATA_FORMAT IOCTL */
#define RMNET_IOCTL_INGRESS_FORMAT_MAP                 (1<<1)
#define RMNET_IOCTL_INGRESS_FORMAT_DEAGGREGATION       (1<<2)
#define RMNET_IOCTL_INGRESS_FORMAT_DEMUXING            (1<<3)
#define RMNET_IOCTL_INGRESS_FORMAT_CHECKSUM            (1<<4)
#define RMNET_IOCTL_INGRESS_FORMAT_AGG_DATA            (1<<5)

/* Input values for the RMNET_IOCTL_SET_OFFLOAD */
#define RMNET_IOCTL_OFFLOAD_FORMAT_NONE                   (0)
#define RMNET_IOCTL_COALESCING_FORMAT_TCP              (1<<0)
#define RMNET_IOCTL_COALESCING_FORMAT_UDP              (1<<1)

/* User space may not have this defined. */
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

/**
 * enum rmnet_egress_ep_type - To specify pipe type for egress
 * @RMNET_EGRESS_DEFAULT: WAN Producer pipe
 * @RMNET_EGRESS_LOW_LAT_CTRL: Low latency ctrl producer pipe
 * @RMNET_EGRESS_LOW_LAT_DATA: Low latency data producer pipe
 * Add any number of pipes before max
 */
enum rmnet_egress_ep_type {
	RMNET_EGRESS_DEFAULT	= 0x0000,
	RMNET_EGRESS_LOW_LAT_CTRL	= 0x0001,
	RMNET_EGRESS_LOW_LAT_DATA	= 0x0002,
	RMNET_EGRESS_MAX		= 0x0003,
};


/**
 * enum rmnet_ingress_ep_type - To specify pipe type for ingress
 * @RMNET_INGRESS_COALS: Coalescing Consumer pipe
 * @RMNET_INGRESS_DEFAULT: WAN Consumer pipe
 * @RMNET_INGRESS_LOW_LAT_CTRL: Low latency ctrl consumer pipe
 * @RMNET_INGRESS_LOW_LAT_DATA: Low latency data consumer pipe
 * Add any number of pipes before max
 */
enum rmnet_ingress_ep_type {
	RMNET_INGRESS_COALS		= 0x0000,
	RMNET_INGRESS_DEFAULT		= 0x0001,
	RMNET_INGRESS_LOW_LAT_CTRL	= 0x0002,
	RMNET_INGRESS_LOW_LAT_DATA	= 0x0003,
	RMNET_INGRESS_MAX		= 0x0004,
};

/**
 * enum rmnet_egress_ingress_pipe_setup_status - To give
 *	back the pipe setup status info to netmngr
 * @IPA_PIPE_SETUP_SUCCESS: pipe setup successful
 * @IPA_PIPE_SETUP_FAILURE: pipe setup failure
 * @IPA_PIPE_SETUP_EXISTS: pipe already exists
 */
enum rmnet_egress_ingress_pipe_setup_status {
	IPA_PIPE_SETUP_SUCCESS = 0x0000,
	IPA_PIPE_SETUP_FAILURE = 0x0001,
	IPA_PIPE_SETUP_EXISTS = 0x0002,
};

/**
 * struct rmnet_egress_param - Include all the egress params that
 *				needs to be configured. Structure should have even
 *				__u32 variables or add padding.
 * @egress_ep_type: Should be from rmnet_egress_ep_type
 * @pipe_setup_status: Out parameter.
 *				Need to place below enum
 *				rmnet_egress_ingress_pipe_setup_status
 * @cs_offload_en: Checksum offload (1 - Enable)
 * @aggr_en: Aggregation Enable (1 - Enable)
 * @ulso_en: (1 - Enable)
 * @ipid_min_max_idx(for ULSO): A value from the range [0, 2] determines
 *		the registers pair from which to read the minimum and maximum of
 *		IPv4 packets ID.
 * @int_modt: GSI event ring interrupt moderation time
 *		cycles base interrupt moderation (32KHz clock)
 * @int_modc: GSI event ring interrupt moderation packet counter
 */
struct rmnet_egress_param {
	__u32 egress_ep_type;
	__u32 pipe_setup_status;
	__u32 cs_offload_en;
	__u32 aggr_en;
	__u32 ulso_en;
	__u32 ipid_min_max_idx;
	__u32 int_modt;
	__u32 int_modc;
};

/**
 * struct rmnet_ingress_param - Include all the ingress params that
 *				needs to be configured. Structure should have even
 *				__u32 variables or add padding.
 * @ingress_ep_type: Should be from rmnet_ingress_ep_type
 * @pipe_setup_status: Out parameter.
 *				Need to place below enum
 *				rmnet_egress_ingress_pipe_setup_status
 * @cs_offload_en: Checksum offload (1 - Enable)
 * @buff_size: Actual buff size of rx_pkt
 * @agg_byte_limit: Aggregation byte limit
 * @agg_time_limit: Aggregation time limit
 * @agg_pkt_limit: Aggregation packet limit
 * @int_modt: GSI event ring interrupt moderation time
 *		cycles base interrupt moderation (32KHz clock)
 * @int_modc: GSI event ring interrupt moderation packet counter
 * @padding: To make it 64 bit packed structure
 */
struct rmnet_ingress_param {
	__u32 ingress_ep_type;
	__u32 pipe_setup_status;
	__u32 cs_offload_en;
	__u32 buff_size;
	__u32 agg_byte_limit;
	__u32 agg_time_limit;
	__u32 agg_pkt_limit;
	__u32 int_modt;
	__u32 int_modc;
	__u32 padding;
};

/**
 * Following uapi coding convention here
 * struct mystruct {
 *		__u64 pointer;
 * };
 *
 * In userspace code:
 *		mystruct.pointer = (__u64)(uintptr_t) &pointer;
 * In kernelspace code:
 *		copy_from_user(&struct, u64_to_user_ptr(mystruct.pointer), size);
 */

/**
 * struct ingress_format_v2 - To include all the ingress params that
 *				needs to be configured. Structure should have even
 *				__u32 variables or add padding.
 * @ingress_param_ptr: Should be rmnet_ingress_param pointer.
 *					Array of ingress params for all the pipes.
 * @ingress_param_size: = sizeof(rmnet_ingress_param);
 * @number_of_eps: Number of ep_types, should be = RMNET_INGRESS_MAX
 */
struct ingress_format_v2 {
	__u64 ingress_param_ptr;
	__u32 ingress_param_size;
	__u32 number_of_eps;
};

/**
 * struct egress_format_v2 - To include all the egress params that
 *				needs to be configured. Structure should have even
 *				__u32 variables or add padding.
 * @egress_param_ptr: Should be rmnet_egress_param pointer.
 *				Array of egress params for all the pipes.
 * @egress_param_size: = sizeof(rmnet_egress_param);
 * @number_of_eps: Number of ep_types, should be = RMNET_EGRESS_MAX.
 */
struct egress_format_v2 {
	__u64 egress_param_ptr;
	__u32 egress_param_size;
	__u32 number_of_eps;
};

/**
 * struct rmnet_ioctl_extended_s_v2: New structure to include any number of
 *				ioctl of any size. Structure should have even
 *				__u32 variables or add padding.
 * @ioctl_data_size: Eg: For egress ioctl
 *				= sizeof(egress_format_v2)
 * @ioctl_ptr: Has to be typecasted to (__u64)(uintptr_t).
 * @extended_v2_ioctl_type: Should be hash defined above similar
 *				to RMNET_IOCTL_SET_EGRESS_DATA_FORMAT_V2.
 */
struct rmnet_ioctl_extended_s_v2 {
	__u64 ioctl_ptr;
	__u32 ioctl_data_size;
	__u32 extended_v2_ioctl_type;
};

struct rmnet_ioctl_extended_s {
	__u32   extended_ioctl;
	union {
		__u32 data; /* Generic data field for most extended IOCTLs */

		/* Return values for
		 *    RMNET_IOCTL_GET_DRIVER_NAME
		 *    RMNET_IOCTL_GET_DFLT_CONTROL_CHANNEL
		 */
		__s8 if_name[IFNAMSIZ];

		/* Input values for the RMNET_IOCTL_ADD_MUX_CHANNEL IOCTL */
		struct {
			__u32  mux_id;
			__s8    vchannel_name[IFNAMSIZ];
		} rmnet_mux_val;

		/* Input values for the RMNET_IOCTL_FLOW_CONTROL IOCTL */
		struct {
			__u8   flow_mode;
			__u8   mux_id;
		} flow_control_prop;

		/* Return values for RMNET_IOCTL_GET_EP_PAIR */
		struct {
			__u32   consumer_pipe_num;
			__u32   producer_pipe_num;
		} ipa_ep_pair;

		struct {
			__u32 __data; /* Placeholder for legacy data*/
			__u32 agg_size;
			__u32 agg_count;
		} ingress_format;

		/* Input values for the RMNET_IOCTL_SET_OFFLOAD */
		struct {
			__u32   flags;
			__u8    mux_id;
		} offload_params;

		/* Input values for the RMNET_IOCTL_SET_MTU */
		struct {
			__s8    if_name[IFNAMSIZ];
			/* if given non-zero value, mtu has changed */
			__u16   mtu_v4;
			__u16   mtu_v6;
		} mtu_params;
	} u;
};

struct rmnet_ioctl_data_s {
	union {
		__u32	operation_mode;
		__u32	tcm_handle;
	} u;
};

#define RMNET_IOCTL_QOS_MODE_6   (1<<0)
#define RMNET_IOCTL_QOS_MODE_8   (1<<1)

/* QMI QoS header definition */
struct QMI_QOS_HDR_S {
	unsigned char    version;
	unsigned char    flags;
	__u32         flow_id;
} __attribute((__packed__));

/* QMI QoS 8-byte header. */
struct qmi_qos_hdr8_s {
	struct QMI_QOS_HDR_S   hdr;
	__u8                reserved[2];
} __attribute((__packed__));

#endif /* _UAPI_MSM_RMNET_H_ */
