/* Public domain. */

#ifndef _I915_HDCP_INTERFACE_H_
#define _I915_HDCP_INTERFACE_H_

#include <drm/display/drm_hdcp.h>

enum hdcp_wired_protocol {
	HDCP_PROTOCOL_INVALID,
	HDCP_PROTOCOL_HDMI,
	HDCP_PROTOCOL_DP
};

struct hdcp_port_data {
	struct hdcp2_streamid_type *streams;
	uint32_t seq_num_m;
	uint16_t k;
};

struct i915_hdcp_ops {
	int (*initiate_hdcp2_session)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_ake_init *);
	int (*verify_receiver_cert_prepare_km)(struct device *,
	    struct hdcp_port_data *, struct hdcp2_ake_send_cert *, bool *,
	    struct hdcp2_ake_no_stored_km *, size_t *);
	int (*verify_hprime)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_ake_send_hprime *);
	int (*store_pairing_info)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_ake_send_pairing_info *);
	int (*initiate_locality_check)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_lc_init *);
	int (*verify_lprime)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_lc_send_lprime *);
	int (*get_session_key)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_ske_send_eks *);
	int (*repeater_check_flow_prepare_ack)(struct device *,
	    struct hdcp_port_data *, struct hdcp2_rep_send_receiverid_list *,
	    struct hdcp2_rep_send_ack *);
	int (*verify_mprime)(struct device *, struct hdcp_port_data *,
	    struct hdcp2_rep_stream_ready *);
	int (*enable_hdcp_authentication)(struct device *,
	    struct hdcp_port_data *);
	int (*close_hdcp_session)(struct device *, struct hdcp_port_data *);
};

struct i915_hdcp_arbiter {
	void *hdcp_dev;
	const struct i915_hdcp_ops *ops;
};

#endif
