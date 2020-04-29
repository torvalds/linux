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

#if IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS)
#include <linux/stackdepot.h>
#include <linux/timekeeping.h>

enum drm_dp_mst_topology_ref_type {
	DRM_DP_MST_TOPOLOGY_REF_GET,
	DRM_DP_MST_TOPOLOGY_REF_PUT,
};

struct drm_dp_mst_topology_ref_history {
	struct drm_dp_mst_topology_ref_entry {
		enum drm_dp_mst_topology_ref_type type;
		int count;
		ktime_t ts_nsec;
		depot_stack_handle_t backtrace;
	} *entries;
	int len;
};
#endif /* IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS) */

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
 * @port_num: port number
 * @input: if this port is an input port. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @mcs: message capability status - DP 1.2 spec. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @ddps: DisplayPort Device Plug Status - DP 1.2. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @pdt: Peer Device Type. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @ldps: Legacy Device Plug Status. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @dpcd_rev: DPCD revision of device on this port. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @num_sdp_streams: Number of simultaneous streams. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @num_sdp_stream_sinks: Number of stream sinks. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @full_pbn: Max possible bandwidth for this port. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @next: link to next port on this branch device
 * @aux: i2c aux transport to talk to device connected to this port, protected
 * by &drm_dp_mst_topology_mgr.base.lock.
 * @parent: branch device parent of this port
 * @vcpi: Virtual Channel Payload info for this port.
 * @connector: DRM connector this port is connected to. Protected by
 * &drm_dp_mst_topology_mgr.base.lock.
 * @mgr: topology manager this port lives under.
 *
 * This structure represents an MST port endpoint on a device somewhere
 * in the MST topology.
 */
struct drm_dp_mst_port {
	/**
	 * @topology_kref: refcount for this port's lifetime in the topology,
	 * only the DP MST helpers should need to touch this
	 */
	struct kref topology_kref;

	/**
	 * @malloc_kref: refcount for the memory allocation containing this
	 * structure. See drm_dp_mst_get_port_malloc() and
	 * drm_dp_mst_put_port_malloc().
	 */
	struct kref malloc_kref;

#if IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS)
	/**
	 * @topology_ref_history: A history of each topology
	 * reference/dereference. See CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS.
	 */
	struct drm_dp_mst_topology_ref_history topology_ref_history;
#endif

	u8 port_num;
	bool input;
	bool mcs;
	bool ddps;
	u8 pdt;
	bool ldps;
	u8 dpcd_rev;
	u8 num_sdp_streams;
	u8 num_sdp_stream_sinks;
	uint16_t full_pbn;
	struct list_head next;
	/**
	 * @mstb: the branch device connected to this port, if there is one.
	 * This should be considered protected for reading by
	 * &drm_dp_mst_topology_mgr.lock. There are two exceptions to this:
	 * &drm_dp_mst_topology_mgr.up_req_work and
	 * &drm_dp_mst_topology_mgr.work, which do not grab
	 * &drm_dp_mst_topology_mgr.lock during reads but are the only
	 * updaters of this list and are protected from writing concurrently
	 * by &drm_dp_mst_topology_mgr.probe_lock.
	 */
	struct drm_dp_mst_branch *mstb;
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

	bool fec_capable;
};

/**
 * struct drm_dp_mst_branch - MST branch device.
 * @rad: Relative Address to talk to this branch device.
 * @lct: Link count total to talk to this branch device.
 * @num_ports: number of ports on the branch.
 * @msg_slots: one bit per transmitted msg slot.
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
	/**
	 * @topology_kref: refcount for this branch device's lifetime in the
	 * topology, only the DP MST helpers should need to touch this
	 */
	struct kref topology_kref;

	/**
	 * @malloc_kref: refcount for the memory allocation containing this
	 * structure. See drm_dp_mst_get_mstb_malloc() and
	 * drm_dp_mst_put_mstb_malloc().
	 */
	struct kref malloc_kref;

#if IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS)
	/**
	 * @topology_ref_history: A history of each topology
	 * reference/dereference. See CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS.
	 */
	struct drm_dp_mst_topology_ref_history topology_ref_history;
#endif

	/**
	 * @destroy_next: linked-list entry used by
	 * drm_dp_delayed_destroy_work()
	 */
	struct list_head destroy_next;

	u8 rad[8];
	u8 lct;
	int num_ports;

	int msg_slots;
	/**
	 * @ports: the list of ports on this branch device. This should be
	 * considered protected for reading by &drm_dp_mst_topology_mgr.lock.
	 * There are two exceptions to this:
	 * &drm_dp_mst_topology_mgr.up_req_work and
	 * &drm_dp_mst_topology_mgr.work, which do not grab
	 * &drm_dp_mst_topology_mgr.lock during reads but are the only
	 * updaters of this list and are protected from updating the list
	 * concurrently by @drm_dp_mst_topology_mgr.probe_lock
	 */
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
	struct drm_dp_remote_i2c_read_tx {
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
	bool fec_capable;
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
	void (*destroy_connector)(struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_connector *connector);
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

struct drm_dp_vcpi_allocation {
	struct drm_dp_mst_port *port;
	int vcpi;
	int pbn;
	bool dsc_enabled;
	struct list_head next;
};

struct drm_dp_mst_topology_state {
	struct drm_private_state base;
	struct list_head vcpis;
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
	 * @down_rep_recv: Message receiver state for down replies.
	 */
	struct drm_dp_sideband_msg_rx down_rep_recv;
	/**
	 * @up_req_recv: Message receiver state for up requests.
	 */
	struct drm_dp_sideband_msg_rx up_req_recv;

	/**
	 * @lock: protects @mst_state, @mst_primary, @dpcd, and
	 * @payload_id_table_cleared.
	 */
	struct mutex lock;

	/**
	 * @probe_lock: Prevents @work and @up_req_work, the only writers of
	 * &drm_dp_mst_port.mstb and &drm_dp_mst_branch.ports, from racing
	 * while they update the topology.
	 */
	struct mutex probe_lock;

	/**
	 * @mst_state: If this manager is enabled for an MST capable port. False
	 * if no MST sink/branch devices is connected.
	 */
	bool mst_state : 1;

	/**
	 * @payload_id_table_cleared: Whether or not we've cleared the payload
	 * ID table for @mst_primary. Protected by @lock.
	 */
	bool payload_id_table_cleared : 1;

	/**
	 * @is_waiting_for_dwn_reply: whether we're waiting for a down reply.
	 */
	bool is_waiting_for_dwn_reply : 1;

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
	 * VCPI structure itself is &drm_dp_mst_port.vcpi, and the size of
	 * this array is determined by @max_payloads.
	 */
	struct drm_dp_vcpi **proposed_vcpis;
	/**
	 * @payloads: Array of payloads. The size of this array is determined
	 * by @max_payloads.
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
	 * @destroy_port_list: List of to be destroyed connectors.
	 */
	struct list_head destroy_port_list;
	/**
	 * @destroy_branch_device_list: List of to be destroyed branch
	 * devices.
	 */
	struct list_head destroy_branch_device_list;
	/**
	 * @delayed_destroy_lock: Protects @destroy_port_list and
	 * @destroy_branch_device_list.
	 */
	struct mutex delayed_destroy_lock;
	/**
	 * @delayed_destroy_work: Work item to destroy MST port and branch
	 * devices, needed to avoid locking inversion.
	 */
	struct work_struct delayed_destroy_work;

	/**
	 * @up_req_list: List of pending up requests from the topology that
	 * need to be processed, in chronological order.
	 */
	struct list_head up_req_list;
	/**
	 * @up_req_lock: Protects @up_req_list
	 */
	struct mutex up_req_lock;
	/**
	 * @up_req_work: Work item to process up requests received from the
	 * topology. Needed to avoid blocking hotplug handling and sideband
	 * transmissions.
	 */
	struct work_struct up_req_work;

#if IS_ENABLED(CONFIG_DRM_DEBUG_DP_MST_TOPOLOGY_REFS)
	/**
	 * @topology_ref_history_lock: protects
	 * &drm_dp_mst_port.topology_ref_history and
	 * &drm_dp_mst_branch.topology_ref_history.
	 */
	struct mutex topology_ref_history_lock;
#endif
};

int drm_dp_mst_topology_mgr_init(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_device *dev, struct drm_dp_aux *aux,
				 int max_dpcd_transaction_bytes,
				 int max_payloads, int conn_base_id);

void drm_dp_mst_topology_mgr_destroy(struct drm_dp_mst_topology_mgr *mgr);


int drm_dp_mst_topology_mgr_set_mst(struct drm_dp_mst_topology_mgr *mgr, bool mst_state);


int drm_dp_mst_hpd_irq(struct drm_dp_mst_topology_mgr *mgr, u8 *esi, bool *handled);


int
drm_dp_mst_detect_port(struct drm_connector *connector,
		       struct drm_modeset_acquire_ctx *ctx,
		       struct drm_dp_mst_topology_mgr *mgr,
		       struct drm_dp_mst_port *port);

bool drm_dp_mst_port_has_audio(struct drm_dp_mst_topology_mgr *mgr,
					struct drm_dp_mst_port *port);
struct edid *drm_dp_mst_get_edid(struct drm_connector *connector, struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port);


int drm_dp_calc_pbn_mode(int clock, int bpp, bool dsc);

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
int __must_check
drm_dp_mst_topology_mgr_resume(struct drm_dp_mst_topology_mgr *mgr,
			       bool sync);

ssize_t drm_dp_mst_dpcd_read(struct drm_dp_aux *aux,
			     unsigned int offset, void *buffer, size_t size);
ssize_t drm_dp_mst_dpcd_write(struct drm_dp_aux *aux,
			      unsigned int offset, void *buffer, size_t size);

int drm_dp_mst_connector_late_register(struct drm_connector *connector,
				       struct drm_dp_mst_port *port);
void drm_dp_mst_connector_early_unregister(struct drm_connector *connector,
					   struct drm_dp_mst_port *port);

struct drm_dp_mst_topology_state *drm_atomic_get_mst_topology_state(struct drm_atomic_state *state,
								    struct drm_dp_mst_topology_mgr *mgr);
int __must_check
drm_dp_atomic_find_vcpi_slots(struct drm_atomic_state *state,
			      struct drm_dp_mst_topology_mgr *mgr,
			      struct drm_dp_mst_port *port, int pbn,
			      int pbn_div);
int drm_dp_mst_atomic_enable_dsc(struct drm_atomic_state *state,
				 struct drm_dp_mst_port *port,
				 int pbn, int pbn_div,
				 bool enable);
int __must_check
drm_dp_mst_add_affected_dsc_crtcs(struct drm_atomic_state *state,
				  struct drm_dp_mst_topology_mgr *mgr);
int __must_check
drm_dp_atomic_release_vcpi_slots(struct drm_atomic_state *state,
				 struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port);
int drm_dp_send_power_updown_phy(struct drm_dp_mst_topology_mgr *mgr,
				 struct drm_dp_mst_port *port, bool power_up);
int __must_check drm_dp_mst_atomic_check(struct drm_atomic_state *state);

void drm_dp_mst_get_port_malloc(struct drm_dp_mst_port *port);
void drm_dp_mst_put_port_malloc(struct drm_dp_mst_port *port);

struct drm_dp_aux *drm_dp_mst_dsc_aux_for_port(struct drm_dp_mst_port *port);

extern const struct drm_private_state_funcs drm_dp_mst_topology_state_funcs;

/**
 * __drm_dp_mst_state_iter_get - private atomic state iterator function for
 * macro-internal use
 * @state: &struct drm_atomic_state pointer
 * @mgr: pointer to the &struct drm_dp_mst_topology_mgr iteration cursor
 * @old_state: optional pointer to the old &struct drm_dp_mst_topology_state
 * iteration cursor
 * @new_state: optional pointer to the new &struct drm_dp_mst_topology_state
 * iteration cursor
 * @i: int iteration cursor, for macro-internal use
 *
 * Used by for_each_oldnew_mst_mgr_in_state(),
 * for_each_old_mst_mgr_in_state(), and for_each_new_mst_mgr_in_state(). Don't
 * call this directly.
 *
 * Returns:
 * True if the current &struct drm_private_obj is a &struct
 * drm_dp_mst_topology_mgr, false otherwise.
 */
static inline bool
__drm_dp_mst_state_iter_get(struct drm_atomic_state *state,
			    struct drm_dp_mst_topology_mgr **mgr,
			    struct drm_dp_mst_topology_state **old_state,
			    struct drm_dp_mst_topology_state **new_state,
			    int i)
{
	struct __drm_private_objs_state *objs_state = &state->private_objs[i];

	if (objs_state->ptr->funcs != &drm_dp_mst_topology_state_funcs)
		return false;

	*mgr = to_dp_mst_topology_mgr(objs_state->ptr);
	if (old_state)
		*old_state = to_dp_mst_topology_state(objs_state->old_state);
	if (new_state)
		*new_state = to_dp_mst_topology_state(objs_state->new_state);

	return true;
}

/**
 * for_each_oldnew_mst_mgr_in_state - iterate over all DP MST topology
 * managers in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @mgr: &struct drm_dp_mst_topology_mgr iteration cursor
 * @old_state: &struct drm_dp_mst_topology_state iteration cursor for the old
 * state
 * @new_state: &struct drm_dp_mst_topology_state iteration cursor for the new
 * state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all DRM DP MST topology managers in an atomic update,
 * tracking both old and new state. This is useful in places where the state
 * delta needs to be considered, for example in atomic check functions.
 */
#define for_each_oldnew_mst_mgr_in_state(__state, mgr, old_state, new_state, __i) \
	for ((__i) = 0; (__i) < (__state)->num_private_objs; (__i)++) \
		for_each_if(__drm_dp_mst_state_iter_get((__state), &(mgr), &(old_state), &(new_state), (__i)))

/**
 * for_each_old_mst_mgr_in_state - iterate over all DP MST topology managers
 * in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @mgr: &struct drm_dp_mst_topology_mgr iteration cursor
 * @old_state: &struct drm_dp_mst_topology_state iteration cursor for the old
 * state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all DRM DP MST topology managers in an atomic update,
 * tracking only the old state. This is useful in disable functions, where we
 * need the old state the hardware is still in.
 */
#define for_each_old_mst_mgr_in_state(__state, mgr, old_state, __i) \
	for ((__i) = 0; (__i) < (__state)->num_private_objs; (__i)++) \
		for_each_if(__drm_dp_mst_state_iter_get((__state), &(mgr), &(old_state), NULL, (__i)))

/**
 * for_each_new_mst_mgr_in_state - iterate over all DP MST topology managers
 * in an atomic update
 * @__state: &struct drm_atomic_state pointer
 * @mgr: &struct drm_dp_mst_topology_mgr iteration cursor
 * @new_state: &struct drm_dp_mst_topology_state iteration cursor for the new
 * state
 * @__i: int iteration cursor, for macro-internal use
 *
 * This iterates over all DRM DP MST topology managers in an atomic update,
 * tracking only the new state. This is useful in enable functions, where we
 * need the new state the hardware should be in when the atomic commit
 * operation has completed.
 */
#define for_each_new_mst_mgr_in_state(__state, mgr, new_state, __i) \
	for ((__i) = 0; (__i) < (__state)->num_private_objs; (__i)++) \
		for_each_if(__drm_dp_mst_state_iter_get((__state), &(mgr), NULL, &(new_state), (__i)))

#endif
