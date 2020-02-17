/*
 * Copyright © 2014 Red Hat.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */
#ifndef _DRM_DP_MST_HELPER_H_
#define _DRM_DP_MST_HELPER_H_

#include <linux/types.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_atomic.h>

struct drm_dp_mst_branch;

/**
 * struct drm_dp_vcpi - Virtual Channel Payload Identifier
 * @vcpi: Virtual channel ID.
 * @pbn: Payload Bandwidth Number for this channel
 * @aligned_pbn: PBN aligned with slot size
 * @num_slots: number of slots for this PBN
 */
struct drm_dp_vcpi {
	int vcpi;
	int pbn;
	int aligned_pbn;
	int num_slots;
};

/**
 * struct drm_dp_mst_port - MST port
 * @kref: reference count for this port.
 * @port_num: port number
 * @input: if this port is an input port.
 * @mcs: message capability status - DP 1.2 spec.
 * @ddps: DisplayPort Device Plug Status - DP 1.2
 * @pdt: Peer Device Type
 * @ldps: Legacy Device Plug Status
 * @dpcd_rev: DPCD revision of device on this port
 * @num_sdp_streams: Number of simultaneous streams
 * @num_sdp_stream_sinks: Number of stream sinks
 * @available_pbn: Available bandwidth for this port.
 * @next: link to next port on this branch device
 * @mstb: branch device attach below this port
 * @aux: i2c aux transport to talk to device connected to this port.
 * @parent: branch device parent of this port
 * @vcpi: Virtual Channel Payload info for this port.
 * @connector: DRM connector this port is connected to.
 * @mgr: topology manager this port lives under.
 *
 * This structure represents an MST port endpoint on a device somewhere
 * in the MST topology.
 */
struct drm_dp_mst_port {
	struct kref kref;

	u8 port_num;
	bool input;
	bool mcs;
	bool ddps;
	u8 pdt;
	bool ldps;
	u8 dpcd_rev;
	u8 num_sdp_streams;
	u8 num_sdp_stream_sinks;
	uint16_t available_pbn;
	struct list_head next;
	struct drm_dp_mst_branch *mstb; /* pointer to an mstb if this port has one */
	struct drm_dp_aux aux; /* i2c bus for this port? */
	struct drm_dp_mst_branch *parent;

	struct drm_dp_vcpi vcpi;
	struct drm_connector *connector;
	struct drm_dp_mst_topology_mgr *mgr;

	/**
	 * @cached_edid: for DP logical ports - make tiling work by ensuring
	 * that the EDID for all connectors is read immediately.
	 */
	struct edid *cached_edid;
	/**
	 * @has_audio: Tracks whether the sink connector to this port is
	 * audio-capable.
	 */
	bool has_audio;
};

/**
 * struct drm_dp_mst_branch - MST branch device.
 * @kref: reference count for this port.
 * @rad: Relative Address to talk to this branch device.
 * @lct: Link count total to talk to this branch device.
 * @num_ports: number of ports on the branch.
 * @msg_slots: one bit per transmitted msg slot.
 * @ports: linked list of ports on this branch.
 * @port_parent: pointer to the port parent, NULL if toplevel.
 * @mgr: topology manager for this branch device.
 * @tx_slots: transmission slots for this device.
 * @last_seqno: last sequence number used to talk to this.
 * @link_address_sent: if a link address message has been sent to this device yet.
 * @guid: guid for DP 1.2 branch device. port under this branch can be
 * identified by port #.
 *
 * This structure represents an MST branch device, there is one
 * primary branch device at the root, along with any other branches connected
 * to downstream port of parent branches.
 */
struct drm_dp_mst_branch {
	struct kref kref;
	u8 rad[8];
	u8 lct;
	int num_ports;

	int msg_slots;
	struct list_head ports;

	/* list of tx ops queue for this port */
	struct drm_dp_mst_port *port_parent;
	struct drm_dp_mst_topology_mgr *mgr;

	/* slots are protected by mstb->mgr->qlock */
	struct drm_dp_sideband_msg_tx *tx_slots[2];
	int last_seqno;
	bool link_address_sent;

	/* global unique identifier to identify branch devices */
	u8 guid[16];
};


/* sideband msg header - not bit struct */
struct drm_dp_sideband_msg_hdr {
	u8 lct;
	u8 lcr;
	u8 rad[8];
	bool broadcast;
	bool path_msg;
	u8 msg_len;
	bool somt;
	bool eomt;
	bool seqno;
};

struct drm_dp_nak_reply {
	u8 guid[16];
	u8 reason;
	u8 nak_data;
};

struct drm_dp_link_address_ack_reply {
	u8 guid[16];
	u8 nports;
	struct drm_dp_link_addr_reply_port {
		bool input_port;
		u8 peer_device_type;
		u8 port_number;
		bool mcs;
		bool ddps;
		bool legacy_device_plug_status;
		u8 dpcd_revision;
		u8 peer_guid[16];
		u8 num_sdp_streams;
		u8 num_sdp_stream_sinks;
	} ports[16];
};

struct drm_dp_remote_dpcd_read_ack_reply {
	u8 port_number;
	u8 num_bytes;
	u8 bytes[255];
};

struct drm_dp_remote_dpcd_write_ack_reply {
	u8 port_number;
};

struct drm_dp_remote_dpcd_write_nak_reply {
	u8 port_number;
	u8 reason;
	u8 bytes_written_before_failure;
};

struct drm_dp_remote_i2c_read_ack_reply {
	u8 port_number;
	u8 num_bytes;
	u8 bytes[255];
};

struct drm_dp_remote_i2c_read_nak_reply {
	u8 port_number;
	u8 nak_reason;
	u8 i2c_nak_transaction;
};

struct drm_dp_remote_i2c_write_ack_reply {
	u8 port_number;
};


struct drm_dp_sideband_msg_rx {
	u8 chunk[48];
	u8 msg[256];
	u8 curchunk_len;
	u8 curchunk_idx; /* chunk we are parsing now */
	u8 curchunk_hdrlen;
	u8 curlen; /* total length of the msg */
	bool have_somt;
	bool have_eomt;
	struct drm_dp_sideband_msg_hdr initial_hdr;
};

#define DRM_DP_MAX_SDP_STREAMS 16
struct drm_dp_allocate_payload {
	u8 port_number;
	u8 number_sdp_streams;
	u8 vcpi;
	u16 pbn;
	u8 sdp_stream_sink[DRM_DP_MAX_SDP_STREAMS];
};

struct drm_dp_allocate_payload_ack_reply {
	u8 port_number;
	u8 vcpi;
	u16 allocated_pbn;
};

struct drm_dp_connection_status_notify {
	u8 guid[16];
	u8 port_number;
	bool legacy_device_plug_status;
	bool displayport_device_plug_status;
	bool message_capability_status;
	bool input_port;
	u8 peer_device_type;
};

struct drm_dp_remote_dpcd_read {
	u8 port_number;
	u32 dpcd_address;
	u8 num_bytes;
};

struct drm_dp_remote_dpcd_write {
	u8 port_number;
	u32 dpcd_address;
	u8 num_bytes;
	u8 *bytes;
};

#define DP_REMOTE_I2C_READ_MAX_TRANSACTIONS 4
struct drm_dp_remote_i2c_read {
	u8 num_transactions;
	u8 port_number;
	struct {
		u8 i2c_dev_id;
		u8 num_bytes;
		u8 *bytes;
		u8 no_stop_bit;
		u8 i2c_transaction_delay;
	} transactions[DP_REMOTE_I2C_READ_MAX_TRANSACTIONS];
	u8 read_i2c_device_id;
	u8 num_bytes_read;
};

struct drm_dp_remote_i2c_write {
	u8 port_number;
	u8 write_i2c_device_id;
	u8 num_bytes;
	u8 *bytes;
};

/* this covers ENUM_RESOURCES, POWER_DOWN_PHY, POWER_UP_PHY */
struct drm_dp_port_number_req {
	u8 port_number;
};

struct drm_dp_enum_path_resources_ack_reply {
	u8 port_number;
	u16 full_payload_bw_number;
	u16 avail_payload_bw_number;
};

/* covers POWER_DOWN_PHY, POWER_UP_PHY */
struct drm_dp_port_number_rep {
	u8 port_number;
};

struct drm_dp_query_payload {
	u8 port_number;
	u8 vcpi;
};

struct drm_dp_resource_status_notify {
	u8 port_number;
	u8 guid[16];
	u16 available_pbn;
};

struct drm_dp_query_payload_ack_reply {
	u8 port_number;
	u16 allocated_pbn;
};

struct drm_dp_sideband_msg_req_body {
	u8 req_type;
	union ack_req {
		struct drm_dp_connection_status_notify conn_stat;
		struct drm_dp_port_number_req port_num;
		struct drm_dp_resource_status_notify resource_stat;

		struct drm_dp_query_payload query_payload;
		struct drm_dp_allocate_payload allocate_payload;

		struct drm_dp_remote_dpcd_read dpcd_read;
		struct drm_dp_remote_dpcd_write dpcd_write;

		struct drm_dp_remote_i2c_read i2c_read;
		struct drm_dp_remote_i2c_write i2c_write;
	} u;
};

struct drm_dp_sideband_msg_reply_body {
	u8 reply_type;
	u8 req_type;
	union ack_replies {
		struct drm_dp_nak_reply nak;
		struct drm_dp_link_address_ack_reply link_addr;
		struct drm_dp_port_number_rep port_number;

		struct drm_dp_enum_path_resources_ack_reply path_resources;
		struct drm_dp_allocate_payload_ack_reply allocate_payload;
		struct drm_dp_query_payload_ack_reply query_payload;

		struct drm_dp_remote_dpcd_read_ack_reply remote_dpcd_read_ack;
		struct drm_dp_remote_dpcd_write_ack_reply remote_dpcd_write_ack;
		struct drm_dp_remote_dpcd_write_nak_reply remote_dpcd_write_nack;

		struct drm_dp_remote_i2c_read_ack_reply remote_i2c_read_ack;
		struct drm_dp_remote_i2c_read_nak_reply remote_i2c_read_nack;
		struct drm_dp_remote_i2c_write_ack_reply remote_i2c_write_ack;
	} u;
};

/* msg is queued to be put into a slot */
#define DRM_DP_SIDEBAND_TX_QUEUED 0
/* msg has started transmitting on a slot - still on msgq */
#define DRM_DP_SIDEBAND_TX_START_SEND 1
/* msg has finished transmitting on a slot - removed from msgq only in slot */
#define DRM_DP_SIDEBAND_TX_SENT 2
/* msg has received a response - removed from slot */
#define DRM_DP_SIDEBAND_TX_RX 3
#define DRM_DP_SIDEBAND_TX_TIMEOUT 4

struct drm_dp_sideband_msg_tx {
	u8 msg[256];
	u8 chunk[48];
	u8 cur_offset;
	u8 cur_len;
	struct drm_dp_mst_branch *dst;
	struct list_head next;
	int seqno;
	int state;
	bool path_msg;
	struct drm_dp_sideband_msg_reply_body reply;
};

/* sideband msg handler */
struct drm_dp_mst_topology_mgr;
struct drm_dp_mst_topology_cbs {
	/* create a connector for a port */
	struct drm_connector *(*add_connector)(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port, const char *path);
	void (*register_connector)(struct drm_connector *connector);
	void (*destroy_connector)(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_connector *connector);
	void (*hotplug)(struct drm_dp_mst_topology_mgr *mgr);

};

#define DP_MAX_PAYLOAD (sizeof(unsigned long) * 8)

#define DP_PAYLOAD_LOCAL 1
#define DP_PAYLOAD_REMOTE 2
#define DP_PAYLOAD_DELETE_LOCAL 3

struct drm_dp_payload {
	int payload_state;
	int start_slot;
	int num_slots;
	int vcpi;
};

#define to_dp_mst_topology_state(x) container_of(x, struct drm_dp_mst_topology_state, base)

struct drm_dp_mst_topology_state {
	struct drm_private_state base;
	int avail_slots;
	struct drm_atomic_state *state;
	struct drm_dp_mst_topology_mgr *mgr;
};

#define to_dp_mst_topology_mgr(x) container_of(x, struct drm_dp_mst_topology_mgr, base)

/**
 * struct drm_dp_mst_topology_mgr - DisplayPort MST manager
 *
 * This struct represents the toplevel displayport MST topology manager.
 * There should be one instance of this for every MST capable DP connector
 * on the GPU.
 */
struct drm_dp_mst_topology_mgr {
	/**
	 * @base: Base private object for atomic
	 */
	struct drm_private_obj base;

	/**
	 * @dev: device pointer for adding i2c devices etc.
	 */
	struct drm_device *dev;
	/**
	 * @cbs: callbacks for connector addition and destruction.
	 */
	const struct drm_dp_mst_topology_cbs *cbs;
	/**
	 * @max_dpcd_transaction_bytes: maximum number of bytes to read/write
	 * in one go.
	 */
	int max_dpcd_transaction_bytes;
	/**
	 * @aux: AUX channel for the DP MST connector this topolgy mgr is
	 * controlling.
	 */
	struct drm_dp_aux *aux;
	/**
	 * @max_payloads: maximum number of payloads the GPU can generate.
	 */
	int max_payloads;
	/**
	 * @conn_base_id: DRM connector ID this mgr is connected to. Only used
	 * to build the MST connector path value.
	 */
	int conn_base_id;

	/**
	 * @down_rep_recv: Message receiver state for down replies. This and
	 * @up_req_recv are only ever access from the work item, which is
	 * serialised.
	 */
	struct drm_dp_sideband_msg_rx down_rep_recv;
	/**
	 * @up_req_recv: Message receiver state for up requests. This and
	 * @down_rep_recv are only ever access from the work item, which is
	 * serialised.
	 */
	struct drm_dp_sideband_msg_rx up_req_recv;

	/**
	 * @lock: protects mst state, primary, dpcd.
	 */
	struct mutex lock;

	/**
	 * @mst_state: If this manager is enabled for an MST capable port. False
	 * if no MST sink/branch devices is connected.
	 */
	bool mst_state;
	/**
	 * @mst_primary: Pointer to the primary/first branch device.
	 */
	struct drm_dp_mst_branch *mst_primary;

	/**
	 * @dpcd: Cache of DPCD for primary port.
	 */
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	/**
	 * @sink_count: Sink count from DEVICE_SERVICE_IRQ_VECTOR_ESI0.
	 */
	u8 sink_count;
	/**
	 * @pbn_div: PBN to slots divisor.
	 */
	int pbn_div;

	/**
	 * @state: State information for topology manager
	 */
	struct drm_dp_mst_topology_state *state;

	/**
	 * @funcs: Atomic helper callbacks
	 */
	const struct drm_private_state_funcs *funcs;

	/**
	 * @qlock: protects @tx_msg_downq, the &drm_dp_mst_branch.txslost and
	 * &drm_dp_sideband_msg_tx.state once they are queued
	 */
	struct mutex qlock;
	/**
	 * @tx_msg_downq: List of pending down replies.
	 */
	struct list_head tx_msg_downq;

	/**
	 * @payload_lock: Protect payload information.
	 */
	struct mutex payload_lock;
	/**
	 * @proposed_vcpis: Array of pointers for the new VCPI allocation. The
	 * VCPI structure itself is &drm_dp_mst_port.vcpi.
	 */
	struct drm_dp_vcpi **proposed_vcpis;
	/**
	 * @payloads: Array of payloads.
	 */
	struct drm_dp_payload *payloads;
	/**
	 * @payload_mask: Elements of @payloads actually in use. Since
	 * reallocation of active outputs isn't possible gaps can be created by
	 * disabling outputs out of order compared to how they've been enabled.
	 */
	unsigned long payload_mask;
	/**
	 * @vcpi_mask: Similar to @payload_mask, but for @proposed_vcpis.
	 */
	unsigned long vcpi_mask;

	/**
	 * @tx_waitq: Wait to queue stall for the tx worker.
	 */
	wait_queue_head_t tx_waitq;
	/**
	 * @work: Probe work.
	 */
	struct work_struct work;
	/**
	 * @tx_work: Sideband transmit worker. This can nest within the main
	 * @work worker for each transaction @work launches.
	 */
	struct work_struct tx_work;

	/**
	 * @destroy_connector_list: List of to be destroyed connectors.
	 */
	struct list_head destroy_connector_list;
	/**
	 * @destroy_connector_lock: Protects @connector_list.
	 */
	struct mutex destroy_connector_lock;
	/**
	 * @destroy_connector_work: Work item to destroy connectors. Needed to
	 * avoid locking inversion.
	 */
	struct work_struct destroy_connector_work;
};

int drm_dp_mst_topology_mgr_init(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_device *dev, struct drm_dp_aux *aux,
				 int max_dpcd_transaction_bytes,
				 int max_payloads, int conn_base_id);

void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr);


int drm_dp_mst_topology_mgr_set_mst(struct drm_dp_mst_topology_mgr *mgr, bool mst_state);


int drm_dp_mst_hpd_irq(struct drm_dp_mst_topology_mgr *mgr, u8 *esi, bool *handled);


enum drm_connector_status drm_dp_mst_detect_port(struct drm_connector *connector, struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);

bool drm_dp_mst_port_has_audio(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_mst_port *port);
struct edid *drm_dp_mst_get_edid(struct drm_connector *connector, struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);


int drm_dp_calc_pbn_mode(int clock, int bpp);


bool drm_dp_mst_allocate_vcpi(struct drm_dp_mst_topology_mgr *mgr,
			      struct drm_dp_mst_port *port, int pbn, int slots);

int drm_dp_mst_get_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);


void drm_dp_mst_reset_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);


void drm_dp_mst_deallocate_vcpi(struct drm_dp_mst_topology_mgr *mgr,
				struct drm_dp_mst_port *port);


int drm_dp_find_vcpi_slots(struct drm_dp_mst_topology_mgr *mgr,
			   int pbn);


int drm_dp_update_payload_part1(struct drm_dp_mst_topology_mgr *mgr);


int drm_dp_update_payload_part2(struct drm_dp_mst_topology_mgr *mgr);

int drm_dp_check_act_status(struct drm_dp_mst_topology_mgr *mgr);

void drm_dp_mst_dump_topology(struct seq_file *m,
			      struct drm_dp_mst_topology_mgr *mgr);

void drm_dp_mst_topology_mgr_suspend(struct drm_dp_mst_topology_mgr *mgr);
int drm_dp_mst_topology_mgr_resume(struct drm_dp_mst_topology_mgr *mgr);
struct drm_dp_mst_topology_state *drm_atomic_get_mst_topology_state(struct drm_atomic_state *state,
								    struct drm_dp_mst_topology_mgr *mgr);
int drm_dp_atomic_find_vcpi_slots(struct drm_atomic_state *state,
				  struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port, int pbn);
int drm_dp_atomic_release_vcpi_slots(struct drm_atomic_state *state,
				     struct drm_dp_mst_topology_mgr *mgr,
				     int slots);
int drm_dp_send_power_updown_phy(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port, bool power_up);

#endif
