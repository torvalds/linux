/* SPDX-License-Identifier: (GPL-2.0+) */
/*
 * Copyright © 2017-2018 Intel Corporation
 *
 * Authors:
 * Ramalingam C <ramalingam.c@intel.com>
 */

#ifndef _I915_MEI_HDCP_INTERFACE_H_
#define _I915_MEI_HDCP_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/device.h>
#include <drm/drm_hdcp.h>
#include <drm/i915_drm.h>

/**
 * enum hdcp_port_type - HDCP port implementation type defined by ME FW
 * @HDCP_PORT_TYPE_INVALID: Invalid hdcp port type
 * @HDCP_PORT_TYPE_INTEGRATED: In-Host HDCP2.x port
 * @HDCP_PORT_TYPE_LSPCON: HDCP2.2 discrete wired Tx port with LSPCON
 *			   (HDMI 2.0) solution
 * @HDCP_PORT_TYPE_CPDP: HDCP2.2 discrete wired Tx port using the CPDP (DP 1.3)
 *			 solution
 */
enum hdcp_port_type {
	HDCP_PORT_TYPE_INVALID,
	HDCP_PORT_TYPE_INTEGRATED,
	HDCP_PORT_TYPE_LSPCON,
	HDCP_PORT_TYPE_CPDP
};

/**
 * enum hdcp_wired_protocol - HDCP adaptation used on the port
 * @HDCP_PROTOCOL_INVALID: Invalid HDCP adaptation protocol
 * @HDCP_PROTOCOL_HDMI: HDMI adaptation of HDCP used on the port
 * @HDCP_PROTOCOL_DP: DP adaptation of HDCP used on the port
 */
enum hdcp_wired_protocol {
	HDCP_PROTOCOL_INVALID,
	HDCP_PROTOCOL_HDMI,
	HDCP_PROTOCOL_DP
};

/**
 * struct hdcp_port_data - intel specific HDCP port data
 * @port: port index as per I915
 * @port_type: HDCP port type as per ME FW classification
 * @protocol: HDCP adaptation as per ME FW
 * @k: No of streams transmitted on a port. Only on DP MST this is != 1
 * @seq_num_m: Count of RepeaterAuth_Stream_Manage msg propagated.
 *	       Initialized to 0 on AKE_INIT. Incremented after every successful
 *	       transmission of RepeaterAuth_Stream_Manage message. When it rolls
 *	       over re-Auth has to be triggered.
 * @streams: struct hdcp2_streamid_type[k]. Defines the type and id for the
 *	     streams
 */
struct hdcp_port_data {
	enum port port;
	u8 port_type;
	u8 protocol;
	u16 k;
	u32 seq_num_m;
	struct hdcp2_streamid_type *streams;
};

/**
 * struct i915_hdcp_component_ops- ops for HDCP2.2 services.
 * @owner: Module providing the ops
 * @initiate_hdcp2_session: Initiate a Wired HDCP2.2 Tx Session.
 *			    And Prepare AKE_Init.
 * @verify_receiver_cert_prepare_km: Verify the Receiver Certificate
 *				     AKE_Send_Cert and prepare
				     AKE_Stored_Km/AKE_No_Stored_Km
 * @verify_hprime: Verify AKE_Send_H_prime
 * @store_pairing_info: Store pairing info received
 * @initiate_locality_check: Prepare LC_Init
 * @verify_lprime: Verify lprime
 * @get_session_key: Prepare SKE_Send_Eks
 * @repeater_check_flow_prepare_ack: Validate the Downstream topology
 *				     and prepare rep_ack
 * @verify_mprime: Verify mprime
 * @enable_hdcp_authentication:  Mark a port as authenticated.
 * @close_hdcp_session: Close the Wired HDCP Tx session per port.
 *			This also disables the authenticated state of the port.
 */
struct i915_hdcp_component_ops {
	/**
	 * @owner: mei_hdcp module
	 */
	struct module *owner;

	int (*initiate_hdcp2_session)(struct device *dev,
				      struct hdcp_port_data *data,
				      struct hdcp2_ake_init *ake_data);
	int (*verify_receiver_cert_prepare_km)(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_ake_send_cert
								*rx_cert,
					       bool *km_stored,
					       struct hdcp2_ake_no_stored_km
								*ek_pub_km,
					       size_t *msg_sz);
	int (*verify_hprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_ake_send_hprime *rx_hprime);
	int (*store_pairing_info)(struct device *dev,
				  struct hdcp_port_data *data,
				  struct hdcp2_ake_send_pairing_info
								*pairing_info);
	int (*initiate_locality_check)(struct device *dev,
				       struct hdcp_port_data *data,
				       struct hdcp2_lc_init *lc_init_data);
	int (*verify_lprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_lc_send_lprime *rx_lprime);
	int (*get_session_key)(struct device *dev,
			       struct hdcp_port_data *data,
			       struct hdcp2_ske_send_eks *ske_data);
	int (*repeater_check_flow_prepare_ack)(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_rep_send_receiverid_list
								*rep_topology,
					       struct hdcp2_rep_send_ack
								*rep_send_ack);
	int (*verify_mprime)(struct device *dev,
			     struct hdcp_port_data *data,
			     struct hdcp2_rep_stream_ready *stream_ready);
	int (*enable_hdcp_authentication)(struct device *dev,
					  struct hdcp_port_data *data);
	int (*close_hdcp_session)(struct device *dev,
				  struct hdcp_port_data *data);
};

/**
 * struct i915_hdcp_component_master - Used for communication between i915
 * and mei_hdcp drivers for the HDCP2.2 services
 * @mei_dev: device that provide the HDCP2.2 service from MEI Bus.
 * @hdcp_ops: Ops implemented by mei_hdcp driver, used by i915 driver.
 */
struct i915_hdcp_comp_master {
	struct device *mei_dev;
	const struct i915_hdcp_component_ops *ops;

	/* To protect the above members. */
	struct mutex mutex;
};

#endif /* _I915_MEI_HDCP_INTERFACE_H_ */
