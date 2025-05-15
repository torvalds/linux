/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#ifndef _PDS_CORE_ADMINQ_H_
#define _PDS_CORE_ADMINQ_H_

#define PDSC_ADMINQ_MAX_POLL_INTERVAL	256

enum pds_core_adminq_flags {
	PDS_AQ_FLAG_FASTPOLL	= BIT(1),	/* completion poll at 1ms */
};

/*
 * enum pds_core_adminq_opcode - AdminQ command opcodes
 * These commands are only processed on AdminQ, not available in devcmd
 */
enum pds_core_adminq_opcode {
	PDS_AQ_CMD_NOP			= 0,

	/* Client control */
	PDS_AQ_CMD_CLIENT_REG		= 6,
	PDS_AQ_CMD_CLIENT_UNREG		= 7,
	PDS_AQ_CMD_CLIENT_CMD		= 8,

	/* LIF commands */
	PDS_AQ_CMD_LIF_IDENTIFY		= 20,
	PDS_AQ_CMD_LIF_INIT		= 21,
	PDS_AQ_CMD_LIF_RESET		= 22,
	PDS_AQ_CMD_LIF_GETATTR		= 23,
	PDS_AQ_CMD_LIF_SETATTR		= 24,
	PDS_AQ_CMD_LIF_SETPHC		= 25,

	PDS_AQ_CMD_RX_MODE_SET		= 30,
	PDS_AQ_CMD_RX_FILTER_ADD	= 31,
	PDS_AQ_CMD_RX_FILTER_DEL	= 32,

	/* Queue commands */
	PDS_AQ_CMD_Q_IDENTIFY		= 39,
	PDS_AQ_CMD_Q_INIT		= 40,
	PDS_AQ_CMD_Q_CONTROL		= 41,

	/* SR/IOV commands */
	PDS_AQ_CMD_VF_GETATTR		= 60,
	PDS_AQ_CMD_VF_SETATTR		= 61,
};

/*
 * enum pds_core_notifyq_opcode - NotifyQ event codes
 */
enum pds_core_notifyq_opcode {
	PDS_EVENT_LINK_CHANGE		= 1,
	PDS_EVENT_RESET			= 2,
	PDS_EVENT_XCVR			= 5,
	PDS_EVENT_CLIENT		= 6,
};

#define PDS_COMP_COLOR_MASK  0x80

/**
 * struct pds_core_notifyq_event - Generic event reporting structure
 * @eid:   event number
 * @ecode: event code
 *
 * This is the generic event report struct from which the other
 * actual events will be formed.
 */
struct pds_core_notifyq_event {
	__le64 eid;
	__le16 ecode;
};

/**
 * struct pds_core_link_change_event - Link change event notification
 * @eid:		event number
 * @ecode:		event code = PDS_EVENT_LINK_CHANGE
 * @link_status:	link up/down, with error bits
 * @link_speed:		speed of the network link
 *
 * Sent when the network link state changes between UP and DOWN
 */
struct pds_core_link_change_event {
	__le64 eid;
	__le16 ecode;
	__le16 link_status;
	__le32 link_speed;	/* units of 1Mbps: e.g. 10000 = 10Gbps */
};

/**
 * struct pds_core_reset_event - Reset event notification
 * @eid:		event number
 * @ecode:		event code = PDS_EVENT_RESET
 * @reset_code:		reset type
 * @state:		0=pending, 1=complete, 2=error
 *
 * Sent when the NIC or some subsystem is going to be or
 * has been reset.
 */
struct pds_core_reset_event {
	__le64 eid;
	__le16 ecode;
	u8     reset_code;
	u8     state;
};

/**
 * struct pds_core_client_event - Client event notification
 * @eid:		event number
 * @ecode:		event code = PDS_EVENT_CLIENT
 * @client_id:          client to sent event to
 * @client_event:       wrapped event struct for the client
 *
 * Sent when an event needs to be passed on to a client
 */
struct pds_core_client_event {
	__le64 eid;
	__le16 ecode;
	__le16 client_id;
	u8     client_event[54];
};

/**
 * struct pds_core_notifyq_cmd - Placeholder for building qcq
 * @data:      anonymous field for building the qcq
 */
struct pds_core_notifyq_cmd {
	__le32 data;	/* Not used but needed for qcq structure */
};

/*
 * union pds_core_notifyq_comp - Overlay of notifyq event structures
 */
union pds_core_notifyq_comp {
	struct {
		__le64 eid;
		__le16 ecode;
	};
	struct pds_core_notifyq_event     event;
	struct pds_core_link_change_event link_change;
	struct pds_core_reset_event       reset;
	u8     data[64];
};

#define PDS_DEVNAME_LEN		32
/**
 * struct pds_core_client_reg_cmd - Register a new client with DSC
 * @opcode:         opcode PDS_AQ_CMD_CLIENT_REG
 * @rsvd:           word boundary padding
 * @devname:        text name of client device
 * @vif_type:       what type of device (enum pds_core_vif_types)
 *
 * Tell the DSC of the new client, and receive a client_id from DSC.
 */
struct pds_core_client_reg_cmd {
	u8     opcode;
	u8     rsvd[3];
	char   devname[PDS_DEVNAME_LEN];
	u8     vif_type;
};

/**
 * struct pds_core_client_reg_comp - Client registration completion
 * @status:     Status of the command (enum pdc_core_status_code)
 * @rsvd:       Word boundary padding
 * @comp_index: Index in the descriptor ring for which this is the completion
 * @client_id:  New id assigned by DSC
 * @rsvd1:      Word boundary padding
 * @color:      Color bit
 */
struct pds_core_client_reg_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	__le16 client_id;
	u8     rsvd1[9];
	u8     color;
};

/**
 * struct pds_core_client_unreg_cmd - Unregister a client from DSC
 * @opcode:     opcode PDS_AQ_CMD_CLIENT_UNREG
 * @rsvd:       word boundary padding
 * @client_id:  id of client being removed
 *
 * Tell the DSC this client is going away and remove its context
 * This uses the generic completion.
 */
struct pds_core_client_unreg_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 client_id;
};

/**
 * struct pds_core_client_request_cmd - Pass along a wrapped client AdminQ cmd
 * @opcode:     opcode PDS_AQ_CMD_CLIENT_CMD
 * @rsvd:       word boundary padding
 * @client_id:  id of client being removed
 * @client_cmd: the wrapped client command
 *
 * Proxy post an adminq command for the client.
 * This uses the generic completion.
 */
struct pds_core_client_request_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 client_id;
	u8     client_cmd[60];
};

#define PDS_CORE_MAX_FRAGS		16

#define PDS_CORE_QCQ_F_INITED		BIT(0)
#define PDS_CORE_QCQ_F_SG		BIT(1)
#define PDS_CORE_QCQ_F_INTR		BIT(2)
#define PDS_CORE_QCQ_F_TX_STATS		BIT(3)
#define PDS_CORE_QCQ_F_RX_STATS		BIT(4)
#define PDS_CORE_QCQ_F_NOTIFYQ		BIT(5)
#define PDS_CORE_QCQ_F_CMB_RINGS	BIT(6)
#define PDS_CORE_QCQ_F_CORE		BIT(7)

enum pds_core_lif_type {
	PDS_CORE_LIF_TYPE_DEFAULT = 0,
};

#define PDS_CORE_IFNAMSIZ		16

/**
 * enum pds_core_logical_qtype - Logical Queue Types
 * @PDS_CORE_QTYPE_ADMINQ:    Administrative Queue
 * @PDS_CORE_QTYPE_NOTIFYQ:   Notify Queue
 * @PDS_CORE_QTYPE_RXQ:       Receive Queue
 * @PDS_CORE_QTYPE_TXQ:       Transmit Queue
 * @PDS_CORE_QTYPE_EQ:        Event Queue
 * @PDS_CORE_QTYPE_MAX:       Max queue type supported
 */
enum pds_core_logical_qtype {
	PDS_CORE_QTYPE_ADMINQ  = 0,
	PDS_CORE_QTYPE_NOTIFYQ = 1,
	PDS_CORE_QTYPE_RXQ     = 2,
	PDS_CORE_QTYPE_TXQ     = 3,
	PDS_CORE_QTYPE_EQ      = 4,

	PDS_CORE_QTYPE_MAX     = 16   /* don't change - used in struct size */
};

/**
 * union pds_core_lif_config - LIF configuration
 * @state:	    LIF state (enum pds_core_lif_state)
 * @rsvd:           Word boundary padding
 * @name:	    LIF name
 * @rsvd2:          Word boundary padding
 * @features:	    LIF features active (enum pds_core_hw_features)
 * @queue_count:    Queue counts per queue-type
 * @words:          Full union buffer size
 */
union pds_core_lif_config {
	struct {
		u8     state;
		u8     rsvd[3];
		char   name[PDS_CORE_IFNAMSIZ];
		u8     rsvd2[12];
		__le64 features;
		__le32 queue_count[PDS_CORE_QTYPE_MAX];
	} __packed;
	__le32 words[64];
};

/**
 * struct pds_core_lif_status - LIF status register
 * @eid:	     most recent NotifyQ event id
 * @rsvd:            full struct size
 */
struct pds_core_lif_status {
	__le64 eid;
	u8     rsvd[56];
};

/**
 * struct pds_core_lif_info - LIF info structure
 * @config:	LIF configuration structure
 * @status:	LIF status structure
 */
struct pds_core_lif_info {
	union pds_core_lif_config config;
	struct pds_core_lif_status status;
};

/**
 * struct pds_core_lif_identity - LIF identity information (type-specific)
 * @features:		LIF features (see enum pds_core_hw_features)
 * @version:		Identify structure version
 * @hw_index:		LIF hardware index
 * @rsvd:		Word boundary padding
 * @max_nb_sessions:	Maximum number of sessions supported
 * @rsvd2:		buffer padding
 * @config:		LIF config struct with features, q counts
 */
struct pds_core_lif_identity {
	__le64 features;
	u8     version;
	u8     hw_index;
	u8     rsvd[2];
	__le32 max_nb_sessions;
	u8     rsvd2[120];
	union pds_core_lif_config config;
};

/**
 * struct pds_core_lif_identify_cmd - Get LIF identity info command
 * @opcode:	Opcode PDS_AQ_CMD_LIF_IDENTIFY
 * @type:	LIF type (enum pds_core_lif_type)
 * @client_id:	Client identifier
 * @ver:	Version of identify returned by device
 * @rsvd:       Word boundary padding
 * @ident_pa:	DMA address to receive identity info
 *
 * Firmware will copy LIF identity data (struct pds_core_lif_identity)
 * into the buffer address given.
 */
struct pds_core_lif_identify_cmd {
	u8     opcode;
	u8     type;
	__le16 client_id;
	u8     ver;
	u8     rsvd[3];
	__le64 ident_pa;
};

/**
 * struct pds_core_lif_identify_comp - LIF identify command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @ver:	Version of identify returned by device
 * @bytes:	Bytes copied into the buffer
 * @rsvd:       Word boundary padding
 * @color:      Color bit
 */
struct pds_core_lif_identify_comp {
	u8     status;
	u8     ver;
	__le16 bytes;
	u8     rsvd[11];
	u8     color;
};

/**
 * struct pds_core_lif_init_cmd - LIF init command
 * @opcode:	Opcode PDS_AQ_CMD_LIF_INIT
 * @type:	LIF type (enum pds_core_lif_type)
 * @client_id:	Client identifier
 * @rsvd:       Word boundary padding
 * @info_pa:	Destination address for LIF info (struct pds_core_lif_info)
 */
struct pds_core_lif_init_cmd {
	u8     opcode;
	u8     type;
	__le16 client_id;
	__le32 rsvd;
	__le64 info_pa;
};

/**
 * struct pds_core_lif_init_comp - LIF init command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:       Word boundary padding
 * @hw_index:	Hardware index of the initialized LIF
 * @rsvd1:      Word boundary padding
 * @color:      Color bit
 */
struct pds_core_lif_init_comp {
	u8 status;
	u8 rsvd;
	__le16 hw_index;
	u8     rsvd1[11];
	u8     color;
};

/**
 * struct pds_core_lif_reset_cmd - LIF reset command
 * Will reset only the specified LIF.
 * @opcode:	Opcode PDS_AQ_CMD_LIF_RESET
 * @rsvd:       Word boundary padding
 * @client_id:	Client identifier
 */
struct pds_core_lif_reset_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 client_id;
};

/**
 * enum pds_core_lif_attr - List of LIF attributes
 * @PDS_CORE_LIF_ATTR_STATE:		LIF state attribute
 * @PDS_CORE_LIF_ATTR_NAME:		LIF name attribute
 * @PDS_CORE_LIF_ATTR_FEATURES:		LIF features attribute
 * @PDS_CORE_LIF_ATTR_STATS_CTRL:	LIF statistics control attribute
 */
enum pds_core_lif_attr {
	PDS_CORE_LIF_ATTR_STATE		= 0,
	PDS_CORE_LIF_ATTR_NAME		= 1,
	PDS_CORE_LIF_ATTR_FEATURES	= 4,
	PDS_CORE_LIF_ATTR_STATS_CTRL	= 6,
};

/**
 * struct pds_core_lif_setattr_cmd - Set LIF attributes on the NIC
 * @opcode:	Opcode PDS_AQ_CMD_LIF_SETATTR
 * @attr:	Attribute type (enum pds_core_lif_attr)
 * @client_id:	Client identifier
 * @state:	LIF state (enum pds_core_lif_state)
 * @name:	The name string, 0 terminated
 * @features:	Features (enum pds_core_hw_features)
 * @stats_ctl:	Stats control commands (enum pds_core_stats_ctl_cmd)
 * @rsvd:       Command Buffer padding
 */
struct pds_core_lif_setattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 client_id;
	union {
		u8      state;
		char    name[PDS_CORE_IFNAMSIZ];
		__le64  features;
		u8      stats_ctl;
		u8      rsvd[60];
	} __packed;
};

/**
 * struct pds_core_lif_setattr_comp - LIF set attr command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:       Word boundary padding
 * @comp_index: Index in the descriptor ring for which this is the completion
 * @features:	Features (enum pds_core_hw_features)
 * @rsvd2:      Word boundary padding
 * @color:	Color bit
 */
struct pds_core_lif_setattr_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	union {
		__le64  features;
		u8      rsvd2[11];
	} __packed;
	u8     color;
};

/**
 * struct pds_core_lif_getattr_cmd - Get LIF attributes from the NIC
 * @opcode:	Opcode PDS_AQ_CMD_LIF_GETATTR
 * @attr:	Attribute type (enum pds_core_lif_attr)
 * @client_id:	Client identifier
 */
struct pds_core_lif_getattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 client_id;
};

/**
 * struct pds_core_lif_getattr_comp - LIF get attr command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:       Word boundary padding
 * @comp_index: Index in the descriptor ring for which this is the completion
 * @state:	LIF state (enum pds_core_lif_state)
 * @name:	LIF name string, 0 terminated
 * @features:	Features (enum pds_core_hw_features)
 * @rsvd2:      Word boundary padding
 * @color:	Color bit
 */
struct pds_core_lif_getattr_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	union {
		u8      state;
		__le64  features;
		u8      rsvd2[11];
	} __packed;
	u8     color;
};

/**
 * union pds_core_q_identity - Queue identity information
 * @version:	Queue type version that can be used with FW
 * @supported:	Bitfield of queue versions, first bit = ver 0
 * @rsvd:       Word boundary padding
 * @features:	Queue features
 * @desc_sz:	Descriptor size
 * @comp_sz:	Completion descriptor size
 * @rsvd2:      Word boundary padding
 */
struct pds_core_q_identity {
	u8      version;
	u8      supported;
	u8      rsvd[6];
#define PDS_CORE_QIDENT_F_CQ	0x01	/* queue has completion ring */
	__le64  features;
	__le16  desc_sz;
	__le16  comp_sz;
	u8      rsvd2[6];
};

/**
 * struct pds_core_q_identify_cmd - queue identify command
 * @opcode:	Opcode PDS_AQ_CMD_Q_IDENTIFY
 * @type:	Logical queue type (enum pds_core_logical_qtype)
 * @client_id:	Client identifier
 * @ver:	Highest queue type version that the driver supports
 * @rsvd:       Word boundary padding
 * @ident_pa:   DMA address to receive the data (struct pds_core_q_identity)
 */
struct pds_core_q_identify_cmd {
	u8     opcode;
	u8     type;
	__le16 client_id;
	u8     ver;
	u8     rsvd[3];
	__le64 ident_pa;
};

/**
 * struct pds_core_q_identify_comp - queue identify command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:       Word boundary padding
 * @comp_index:	Index in the descriptor ring for which this is the completion
 * @ver:	Queue type version that can be used with FW
 * @rsvd1:      Word boundary padding
 * @color:      Color bit
 */
struct pds_core_q_identify_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     ver;
	u8     rsvd1[10];
	u8     color;
};

/**
 * struct pds_core_q_init_cmd - Queue init command
 * @opcode:	  Opcode PDS_AQ_CMD_Q_INIT
 * @type:	  Logical queue type
 * @client_id:	  Client identifier
 * @ver:	  Queue type version
 * @rsvd:         Word boundary padding
 * @index:	  (LIF, qtype) relative admin queue index
 * @intr_index:	  Interrupt control register index, or Event queue index
 * @pid:	  Process ID
 * @flags:
 *    IRQ:	  Interrupt requested on completion
 *    ENA:	  Enable the queue.  If ENA=0 the queue is initialized
 *		  but remains disabled, to be later enabled with the
 *		  Queue Enable command. If ENA=1, then queue is
 *		  initialized and then enabled.
 * @cos:	  Class of service for this queue
 * @ring_size:	  Queue ring size, encoded as a log2(size), in
 *		  number of descriptors.  The actual ring size is
 *		  (1 << ring_size).  For example, to select a ring size
 *		  of 64 descriptors write ring_size = 6. The minimum
 *		  ring_size value is 2 for a ring of 4 descriptors.
 *		  The maximum ring_size value is 12 for a ring of 4k
 *		  descriptors. Values of ring_size <2 and >12 are
 *		  reserved.
 * @ring_base:	  Queue ring base address
 * @cq_ring_base: Completion queue ring base address
 */
struct pds_core_q_init_cmd {
	u8     opcode;
	u8     type;
	__le16 client_id;
	u8     ver;
	u8     rsvd[3];
	__le32 index;
	__le16 pid;
	__le16 intr_index;
	__le16 flags;
#define PDS_CORE_QINIT_F_IRQ	0x01	/* Request interrupt on completion */
#define PDS_CORE_QINIT_F_ENA	0x02	/* Enable the queue */
	u8     cos;
#define PDS_CORE_QSIZE_MIN_LG2	2
#define PDS_CORE_QSIZE_MAX_LG2	12
	u8     ring_size;
	__le64 ring_base;
	__le64 cq_ring_base;
} __packed;

/**
 * struct pds_core_q_init_comp - Queue init command completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd:       Word boundary padding
 * @comp_index:	Index in the descriptor ring for which this is the completion
 * @hw_index:	Hardware Queue ID
 * @hw_type:	Hardware Queue type
 * @rsvd2:      Word boundary padding
 * @color:	Color
 */
struct pds_core_q_init_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	__le32 hw_index;
	u8     hw_type;
	u8     rsvd2[6];
	u8     color;
};

/*
 * enum pds_vdpa_cmd_opcode - vDPA Device commands
 */
enum pds_vdpa_cmd_opcode {
	PDS_VDPA_CMD_INIT		= 48,
	PDS_VDPA_CMD_IDENT		= 49,
	PDS_VDPA_CMD_RESET		= 51,
	PDS_VDPA_CMD_VQ_RESET		= 52,
	PDS_VDPA_CMD_VQ_INIT		= 53,
	PDS_VDPA_CMD_STATUS_UPDATE	= 54,
	PDS_VDPA_CMD_SET_FEATURES	= 55,
	PDS_VDPA_CMD_SET_ATTR		= 56,
};

/**
 * struct pds_vdpa_cmd - generic command
 * @opcode:	Opcode
 * @vdpa_index:	Index for vdpa subdevice
 * @vf_id:	VF id
 */
struct pds_vdpa_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
};

/**
 * struct pds_vdpa_init_cmd - INIT command
 * @opcode:	Opcode PDS_VDPA_CMD_INIT
 * @vdpa_index: Index for vdpa subdevice
 * @vf_id:	VF id
 */
struct pds_vdpa_init_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
};

/**
 * struct pds_vdpa_ident - vDPA identification data
 * @hw_features:	vDPA features supported by device
 * @max_vqs:		max queues available (2 queues for a single queuepair)
 * @max_qlen:		log(2) of maximum number of descriptors
 * @min_qlen:		log(2) of minimum number of descriptors
 *
 * This struct is used in a DMA block that is set up for the PDS_VDPA_CMD_IDENT
 * transaction.  Set up the DMA block and send the address in the IDENT cmd
 * data, the DSC will write the ident information, then we can remove the DMA
 * block after reading the answer.  If the completion status is 0, then there
 * is valid information, else there was an error and the data should be invalid.
 */
struct pds_vdpa_ident {
	__le64 hw_features;
	__le16 max_vqs;
	__le16 max_qlen;
	__le16 min_qlen;
};

/**
 * struct pds_vdpa_ident_cmd - IDENT command
 * @opcode:	Opcode PDS_VDPA_CMD_IDENT
 * @rsvd:       Word boundary padding
 * @vf_id:	VF id
 * @len:	length of ident info DMA space
 * @ident_pa:	address for DMA of ident info (struct pds_vdpa_ident)
 *			only used for this transaction, then forgotten by DSC
 */
struct pds_vdpa_ident_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	__le32 len;
	__le64 ident_pa;
};

/**
 * struct pds_vdpa_status_cmd - STATUS_UPDATE command
 * @opcode:	Opcode PDS_VDPA_CMD_STATUS_UPDATE
 * @vdpa_index: Index for vdpa subdevice
 * @vf_id:	VF id
 * @status:	new status bits
 */
struct pds_vdpa_status_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
	u8     status;
};

/**
 * enum pds_vdpa_attr - List of VDPA device attributes
 * @PDS_VDPA_ATTR_MAC:          MAC address
 * @PDS_VDPA_ATTR_MAX_VQ_PAIRS: Max virtqueue pairs
 */
enum pds_vdpa_attr {
	PDS_VDPA_ATTR_MAC          = 1,
	PDS_VDPA_ATTR_MAX_VQ_PAIRS = 2,
};

/**
 * struct pds_vdpa_setattr_cmd - SET_ATTR command
 * @opcode:		Opcode PDS_VDPA_CMD_SET_ATTR
 * @vdpa_index:		Index for vdpa subdevice
 * @vf_id:		VF id
 * @attr:		attribute to be changed (enum pds_vdpa_attr)
 * @pad:		Word boundary padding
 * @mac:		new mac address to be assigned as vdpa device address
 * @max_vq_pairs:	new limit of virtqueue pairs
 */
struct pds_vdpa_setattr_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
	u8     attr;
	u8     pad[3];
	union {
		u8 mac[6];
		__le16 max_vq_pairs;
	} __packed;
};

/**
 * struct pds_vdpa_vq_init_cmd - queue init command
 * @opcode: Opcode PDS_VDPA_CMD_VQ_INIT
 * @vdpa_index:	Index for vdpa subdevice
 * @vf_id:	VF id
 * @qid:	Queue id (bit0 clear = rx, bit0 set = tx, qid=N is ctrlq)
 * @len:	log(2) of max descriptor count
 * @desc_addr:	DMA address of descriptor area
 * @avail_addr:	DMA address of available descriptors (aka driver area)
 * @used_addr:	DMA address of used descriptors (aka device area)
 * @intr_index:	interrupt index
 * @avail_index:	initial device position in available ring
 * @used_index:	initial device position in used ring
 */
struct pds_vdpa_vq_init_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
	__le16 qid;
	__le16 len;
	__le64 desc_addr;
	__le64 avail_addr;
	__le64 used_addr;
	__le16 intr_index;
	__le16 avail_index;
	__le16 used_index;
};

/**
 * struct pds_vdpa_vq_init_comp - queue init completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @hw_qtype:	HW queue type, used in doorbell selection
 * @hw_qindex:	HW queue index, used in doorbell selection
 * @rsvd:	Word boundary padding
 * @color:	Color bit
 */
struct pds_vdpa_vq_init_comp {
	u8     status;
	u8     hw_qtype;
	__le16 hw_qindex;
	u8     rsvd[11];
	u8     color;
};

/**
 * struct pds_vdpa_vq_reset_cmd - queue reset command
 * @opcode:	Opcode PDS_VDPA_CMD_VQ_RESET
 * @vdpa_index:	Index for vdpa subdevice
 * @vf_id:	VF id
 * @qid:	Queue id
 */
struct pds_vdpa_vq_reset_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
	__le16 qid;
};

/**
 * struct pds_vdpa_vq_reset_comp - queue reset completion
 * @status:	Status of the command (enum pds_core_status_code)
 * @rsvd0:	Word boundary padding
 * @avail_index:	current device position in available ring
 * @used_index:	current device position in used ring
 * @rsvd:	Word boundary padding
 * @color:	Color bit
 */
struct pds_vdpa_vq_reset_comp {
	u8     status;
	u8     rsvd0;
	__le16 avail_index;
	__le16 used_index;
	u8     rsvd[9];
	u8     color;
};

/**
 * struct pds_vdpa_set_features_cmd - set hw features
 * @opcode: Opcode PDS_VDPA_CMD_SET_FEATURES
 * @vdpa_index:	Index for vdpa subdevice
 * @vf_id:	VF id
 * @rsvd:       Word boundary padding
 * @features:	Feature bit mask
 */
struct pds_vdpa_set_features_cmd {
	u8     opcode;
	u8     vdpa_index;
	__le16 vf_id;
	__le32 rsvd;
	__le64 features;
};

#define PDS_LM_DEVICE_STATE_LENGTH		65536
#define PDS_LM_CHECK_DEVICE_STATE_LENGTH(X) \
			PDS_CORE_SIZE_CHECK(union, PDS_LM_DEVICE_STATE_LENGTH, X)

/*
 * enum pds_lm_cmd_opcode - Live Migration Device commands
 */
enum pds_lm_cmd_opcode {
	PDS_LM_CMD_HOST_VF_STATUS  = 1,

	/* Device state commands */
	PDS_LM_CMD_STATE_SIZE	   = 16,
	PDS_LM_CMD_SUSPEND         = 18,
	PDS_LM_CMD_SUSPEND_STATUS  = 19,
	PDS_LM_CMD_RESUME          = 20,
	PDS_LM_CMD_SAVE            = 21,
	PDS_LM_CMD_RESTORE         = 22,

	/* Dirty page tracking commands */
	PDS_LM_CMD_DIRTY_STATUS    = 32,
	PDS_LM_CMD_DIRTY_ENABLE    = 33,
	PDS_LM_CMD_DIRTY_DISABLE   = 34,
	PDS_LM_CMD_DIRTY_READ_SEQ  = 35,
	PDS_LM_CMD_DIRTY_WRITE_ACK = 36,
};

/**
 * struct pds_lm_cmd - generic command
 * @opcode:	Opcode
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @rsvd2:	Structure padding to 60 Bytes
 */
struct pds_lm_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     rsvd2[56];
};

/**
 * struct pds_lm_state_size_cmd - STATE_SIZE command
 * @opcode:	Opcode
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 */
struct pds_lm_state_size_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
};

/**
 * struct pds_lm_state_size_comp - STATE_SIZE command completion
 * @status:		Status of the command (enum pds_core_status_code)
 * @rsvd:		Word boundary padding
 * @comp_index:		Index in the desc ring for which this is the completion
 * @size:		Size of the device state
 * @rsvd2:		Word boundary padding
 * @color:		Color bit
 */
struct pds_lm_state_size_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	union {
		__le64 size;
		u8     rsvd2[11];
	} __packed;
	u8     color;
};

enum pds_lm_suspend_resume_type {
	PDS_LM_SUSPEND_RESUME_TYPE_FULL = 0,
	PDS_LM_SUSPEND_RESUME_TYPE_P2P = 1,
};

/**
 * struct pds_lm_suspend_cmd - SUSPEND command
 * @opcode:	Opcode PDS_LM_CMD_SUSPEND
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @type:	Type of suspend (enum pds_lm_suspend_resume_type)
 */
struct pds_lm_suspend_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     type;
};

/**
 * struct pds_lm_suspend_status_cmd - SUSPEND status command
 * @opcode:	Opcode PDS_AQ_CMD_LM_SUSPEND_STATUS
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @type:	Type of suspend (enum pds_lm_suspend_resume_type)
 */
struct pds_lm_suspend_status_cmd {
	u8 opcode;
	u8 rsvd;
	__le16 vf_id;
	u8 type;
};

/**
 * struct pds_lm_resume_cmd - RESUME command
 * @opcode:	Opcode PDS_LM_CMD_RESUME
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @type:	Type of resume (enum pds_lm_suspend_resume_type)
 */
struct pds_lm_resume_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     type;
};

/**
 * struct pds_lm_sg_elem - Transmit scatter-gather (SG) descriptor element
 * @addr:	DMA address of SG element data buffer
 * @len:	Length of SG element data buffer, in bytes
 * @rsvd:	Word boundary padding
 */
struct pds_lm_sg_elem {
	__le64 addr;
	__le32 len;
	__le16 rsvd[2];
};

/**
 * struct pds_lm_save_cmd - SAVE command
 * @opcode:	Opcode PDS_LM_CMD_SAVE
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @rsvd2:	Word boundary padding
 * @sgl_addr:	IOVA address of the SGL to dma the device state
 * @num_sge:	Total number of SG elements
 */
struct pds_lm_save_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     rsvd2[4];
	__le64 sgl_addr;
	__le32 num_sge;
} __packed;

/**
 * struct pds_lm_restore_cmd - RESTORE command
 * @opcode:	Opcode PDS_LM_CMD_RESTORE
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @rsvd2:	Word boundary padding
 * @sgl_addr:	IOVA address of the SGL to dma the device state
 * @num_sge:	Total number of SG elements
 */
struct pds_lm_restore_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     rsvd2[4];
	__le64 sgl_addr;
	__le32 num_sge;
} __packed;

/**
 * union pds_lm_dev_state - device state information
 * @words:	Device state words
 */
union pds_lm_dev_state {
	__le32 words[PDS_LM_DEVICE_STATE_LENGTH / sizeof(__le32)];
};

enum pds_lm_host_vf_status {
	PDS_LM_STA_NONE = 0,
	PDS_LM_STA_IN_PROGRESS,
	PDS_LM_STA_MAX,
};

/**
 * struct pds_lm_dirty_region_info - Memory region info for STATUS and ENABLE
 * @dma_base:		Base address of the DMA-contiguous memory region
 * @page_count:		Number of pages in the memory region
 * @page_size_log2:	Log2 page size in the memory region
 * @rsvd:		Word boundary padding
 */
struct pds_lm_dirty_region_info {
	__le64 dma_base;
	__le32 page_count;
	u8     page_size_log2;
	u8     rsvd[3];
};

/**
 * struct pds_lm_dirty_status_cmd - DIRTY_STATUS command
 * @opcode:		Opcode PDS_LM_CMD_DIRTY_STATUS
 * @rsvd:		Word boundary padding
 * @vf_id:		VF id
 * @max_regions:	Capacity of the region info buffer
 * @rsvd2:		Word boundary padding
 * @regions_dma:	DMA address of the region info buffer
 *
 * The minimum of max_regions (from the command) and num_regions (from the
 * completion) of struct pds_lm_dirty_region_info will be written to
 * regions_dma.
 *
 * The max_regions may be zero, in which case regions_dma is ignored.  In that
 * case, the completion will only report the maximum number of regions
 * supported by the device, and the number of regions currently enabled.
 */
struct pds_lm_dirty_status_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     max_regions;
	u8     rsvd2[3];
	__le64 regions_dma;
} __packed;

/**
 * enum pds_lm_dirty_bmp_type - Type of dirty page bitmap
 * @PDS_LM_DIRTY_BMP_TYPE_NONE: No bitmap / disabled
 * @PDS_LM_DIRTY_BMP_TYPE_SEQ_ACK: Seq/Ack bitmap representation
 */
enum pds_lm_dirty_bmp_type {
	PDS_LM_DIRTY_BMP_TYPE_NONE     = 0,
	PDS_LM_DIRTY_BMP_TYPE_SEQ_ACK  = 1,
};

/**
 * struct pds_lm_dirty_status_comp - STATUS command completion
 * @status:		Status of the command (enum pds_core_status_code)
 * @rsvd:		Word boundary padding
 * @comp_index:		Index in the desc ring for which this is the completion
 * @max_regions:	Maximum number of regions supported by the device
 * @num_regions:	Number of regions currently enabled
 * @bmp_type:		Type of dirty bitmap representation
 * @rsvd2:		Word boundary padding
 * @bmp_type_mask:	Mask of supported bitmap types, bit index per type
 * @rsvd3:		Word boundary padding
 * @color:		Color bit
 *
 * This completion descriptor is used for STATUS, ENABLE, and DISABLE.
 */
struct pds_lm_dirty_status_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     max_regions;
	u8     num_regions;
	u8     bmp_type;
	u8     rsvd2;
	__le32 bmp_type_mask;
	u8     rsvd3[3];
	u8     color;
};

/**
 * struct pds_lm_dirty_enable_cmd - DIRTY_ENABLE command
 * @opcode:		Opcode PDS_LM_CMD_DIRTY_ENABLE
 * @rsvd:		Word boundary padding
 * @vf_id:		VF id
 * @bmp_type:		Type of dirty bitmap representation
 * @num_regions:	Number of entries in the region info buffer
 * @rsvd2:		Word boundary padding
 * @regions_dma:	DMA address of the region info buffer
 *
 * The num_regions must be nonzero, and less than or equal to the maximum
 * number of regions supported by the device.
 *
 * The memory regions should not overlap.
 *
 * The information should be initialized by the driver.  The device may modify
 * the information on successful completion, such as by size-aligning the
 * number of pages in a region.
 *
 * The modified number of pages will be greater than or equal to the page count
 * given in the enable command, and at least as coarsly aligned as the given
 * value.  For example, the count might be aligned to a multiple of 64, but
 * if the value is already a multiple of 128 or higher, it will not change.
 * If the driver requires its own minimum alignment of the number of pages, the
 * driver should account for that already in the region info of this command.
 *
 * This command uses struct pds_lm_dirty_status_comp for its completion.
 */
struct pds_lm_dirty_enable_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     bmp_type;
	u8     num_regions;
	u8     rsvd2[2];
	__le64 regions_dma;
} __packed;

/**
 * struct pds_lm_dirty_disable_cmd - DIRTY_DISABLE command
 * @opcode:	Opcode PDS_LM_CMD_DIRTY_DISABLE
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 *
 * Dirty page tracking will be disabled.  This may be called in any state, as
 * long as dirty page tracking is supported by the device, to ensure that dirty
 * page tracking is disabled.
 *
 * This command uses struct pds_lm_dirty_status_comp for its completion.  On
 * success, num_regions will be zero.
 */
struct pds_lm_dirty_disable_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
};

/**
 * struct pds_lm_dirty_seq_ack_cmd - DIRTY_READ_SEQ or _WRITE_ACK command
 * @opcode:	Opcode PDS_LM_CMD_DIRTY_[READ_SEQ|WRITE_ACK]
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @off_bytes:	Byte offset in the bitmap
 * @len_bytes:	Number of bytes to transfer
 * @num_sge:	Number of DMA scatter gather elements
 * @rsvd2:	Word boundary padding
 * @sgl_addr:	DMA address of scatter gather list
 *
 * Read bytes from the SEQ bitmap, or write bytes into the ACK bitmap.
 *
 * This command treats the entire bitmap as a byte buffer.  It does not
 * distinguish between guest memory regions.  The driver should refer to the
 * number of pages in each region, according to PDS_LM_CMD_DIRTY_STATUS, to
 * determine the region boundaries in the bitmap.  Each region will be
 * represented by exactly the number of bits as the page count for that region,
 * immediately following the last bit of the previous region.
 */
struct pds_lm_dirty_seq_ack_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	__le32 off_bytes;
	__le32 len_bytes;
	__le16 num_sge;
	u8     rsvd2[2];
	__le64 sgl_addr;
} __packed;

/**
 * struct pds_lm_host_vf_status_cmd - HOST_VF_STATUS command
 * @opcode:	Opcode PDS_LM_CMD_HOST_VF_STATUS
 * @rsvd:	Word boundary padding
 * @vf_id:	VF id
 * @status:	Current LM status of host VF driver (enum pds_lm_host_status)
 */
struct pds_lm_host_vf_status_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 vf_id;
	u8     status;
};

enum pds_fwctl_cmd_opcode {
	PDS_FWCTL_CMD_IDENT = 70,
	PDS_FWCTL_CMD_RPC   = 71,
	PDS_FWCTL_CMD_QUERY = 72,
};

/**
 * struct pds_fwctl_cmd - Firmware control command structure
 * @opcode: Opcode
 * @rsvd:   Reserved
 * @ep:     Endpoint identifier
 * @op:     Operation identifier
 */
struct pds_fwctl_cmd {
	u8     opcode;
	u8     rsvd[3];
	__le32 ep;
	__le32 op;
} __packed;

/**
 * struct pds_fwctl_comp - Firmware control completion structure
 * @status:     Status of the firmware control operation
 * @rsvd:       Reserved
 * @comp_index: Completion index in little-endian format
 * @rsvd2:      Reserved
 * @color:      Color bit indicating the state of the completion
 */
struct pds_fwctl_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     rsvd2[11];
	u8     color;
} __packed;

/**
 * struct pds_fwctl_ident_cmd - Firmware control identification command structure
 * @opcode:   Operation code for the command
 * @rsvd:     Reserved
 * @version:  Interface version
 * @rsvd2:    Reserved
 * @len:      Length of the identification data
 * @ident_pa: Physical address of the identification data
 */
struct pds_fwctl_ident_cmd {
	u8     opcode;
	u8     rsvd;
	u8     version;
	u8     rsvd2;
	__le32 len;
	__le64 ident_pa;
} __packed;

/* future feature bits here
 * enum pds_fwctl_features {
 * };
 * (compilers don't like empty enums)
 */

/**
 * struct pds_fwctl_ident - Firmware control identification structure
 * @features:    Supported features (enum pds_fwctl_features)
 * @version:     Interface version
 * @rsvd:        Reserved
 * @max_req_sz:  Maximum request size
 * @max_resp_sz: Maximum response size
 * @max_req_sg_elems:  Maximum number of request SGs
 * @max_resp_sg_elems: Maximum number of response SGs
 */
struct pds_fwctl_ident {
	__le64 features;
	u8     version;
	u8     rsvd[3];
	__le32 max_req_sz;
	__le32 max_resp_sz;
	u8     max_req_sg_elems;
	u8     max_resp_sg_elems;
} __packed;

enum pds_fwctl_query_entity {
	PDS_FWCTL_RPC_ROOT	= 0,
	PDS_FWCTL_RPC_ENDPOINT	= 1,
	PDS_FWCTL_RPC_OPERATION	= 2,
};

#define PDS_FWCTL_RPC_OPCODE_CMD_SHIFT	0
#define PDS_FWCTL_RPC_OPCODE_CMD_MASK	GENMASK(15, PDS_FWCTL_RPC_OPCODE_CMD_SHIFT)
#define PDS_FWCTL_RPC_OPCODE_VER_SHIFT	16
#define PDS_FWCTL_RPC_OPCODE_VER_MASK	GENMASK(23, PDS_FWCTL_RPC_OPCODE_VER_SHIFT)

#define PDS_FWCTL_RPC_OPCODE_GET_CMD(op)  FIELD_GET(PDS_FWCTL_RPC_OPCODE_CMD_MASK, op)
#define PDS_FWCTL_RPC_OPCODE_GET_VER(op)  FIELD_GET(PDS_FWCTL_RPC_OPCODE_VER_MASK, op)

#define PDS_FWCTL_RPC_OPCODE_CMP(op1, op2) \
	(PDS_FWCTL_RPC_OPCODE_GET_CMD(op1) == PDS_FWCTL_RPC_OPCODE_GET_CMD(op2) && \
	 PDS_FWCTL_RPC_OPCODE_GET_VER(op1) <= PDS_FWCTL_RPC_OPCODE_GET_VER(op2))

/*
 * FW command attributes that map to the FWCTL scope values
 */
#define PDSFC_FW_CMD_ATTR_READ               0x00
#define PDSFC_FW_CMD_ATTR_DEBUG_READ         0x02
#define PDSFC_FW_CMD_ATTR_WRITE              0x04
#define PDSFC_FW_CMD_ATTR_DEBUG_WRITE        0x08
#define PDSFC_FW_CMD_ATTR_SYNC               0x10

/**
 * struct pds_fwctl_query_cmd - Firmware control query command structure
 * @opcode: Operation code for the command
 * @entity:  Entity type to query (enum pds_fwctl_query_entity)
 * @version: Version of the query data structure supported by the driver
 * @rsvd:    Reserved
 * @query_data_buf_len: Length of the query data buffer
 * @query_data_buf_pa:  Physical address of the query data buffer
 * @ep:      Endpoint identifier to query  (when entity is PDS_FWCTL_RPC_ENDPOINT)
 * @op:      Operation identifier to query (when entity is PDS_FWCTL_RPC_OPERATION)
 *
 * This structure is used to send a query command to the firmware control
 * interface. The structure is packed to ensure there is no padding between
 * the fields.
 */
struct pds_fwctl_query_cmd {
	u8     opcode;
	u8     entity;
	u8     version;
	u8     rsvd;
	__le32 query_data_buf_len;
	__le64 query_data_buf_pa;
	union {
		__le32 ep;
		__le32 op;
	};
} __packed;

/**
 * struct pds_fwctl_query_comp - Firmware control query completion structure
 * @status:     Status of the query command
 * @rsvd:       Reserved
 * @comp_index: Completion index in little-endian format
 * @version:    Version of the query data structure returned by firmware. This
 *		 should be less than or equal to the version supported by the driver
 * @rsvd2:      Reserved
 * @color:      Color bit indicating the state of the completion
 */
struct pds_fwctl_query_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     version;
	u8     rsvd2[2];
	u8     color;
} __packed;

/**
 * struct pds_fwctl_query_data_endpoint - query data for entity PDS_FWCTL_RPC_ROOT
 * @id: The identifier for the data endpoint
 */
struct pds_fwctl_query_data_endpoint {
	__le32 id;
} __packed;

/**
 * struct pds_fwctl_query_data_operation - query data for entity PDS_FWCTL_RPC_ENDPOINT
 * @id:    Operation identifier
 * @scope: Scope of the operation (enum fwctl_rpc_scope)
 * @rsvd:  Reserved
 */
struct pds_fwctl_query_data_operation {
	__le32 id;
	u8     scope;
	u8     rsvd[3];
} __packed;

/**
 * struct pds_fwctl_query_data - query data structure
 * @version:     Version of the query data structure
 * @rsvd:        Reserved
 * @num_entries: Number of entries in the union
 * @entries:     Array of query data entries, depending on the entity type
 */
struct pds_fwctl_query_data {
	u8      version;
	u8      rsvd[3];
	__le32  num_entries;
	u8      entries[] __counted_by_le(num_entries);
} __packed;

/**
 * struct pds_fwctl_rpc_cmd - Firmware control RPC command
 * @opcode:        opcode PDS_FWCTL_CMD_RPC
 * @rsvd:          Reserved
 * @flags:         Indicates indirect request and/or response handling
 * @ep:            Endpoint identifier
 * @op:            Operation identifier
 * @inline_req0:   Buffer for inline request
 * @inline_req1:   Buffer for inline request
 * @req_pa:        Physical address of request data
 * @req_sz:        Size of the request
 * @req_sg_elems:  Number of request SGs
 * @req_rsvd:      Reserved
 * @inline_req2:   Buffer for inline request
 * @resp_pa:       Physical address of response data
 * @resp_sz:       Size of the response
 * @resp_sg_elems: Number of response SGs
 * @resp_rsvd:     Reserved
 */
struct pds_fwctl_rpc_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 flags;
#define PDS_FWCTL_RPC_IND_REQ		0x1
#define PDS_FWCTL_RPC_IND_RESP		0x2
	__le32 ep;
	__le32 op;
	u8 inline_req0[16];
	union {
		u8 inline_req1[16];
		struct {
			__le64 req_pa;
			__le32 req_sz;
			u8     req_sg_elems;
			u8     req_rsvd[3];
		};
	};
	union {
		u8 inline_req2[16];
		struct {
			__le64 resp_pa;
			__le32 resp_sz;
			u8     resp_sg_elems;
			u8     resp_rsvd[3];
		};
	};
} __packed;

/**
 * struct pds_sg_elem - Transmit scatter-gather (SG) descriptor element
 * @addr:	DMA address of SG element data buffer
 * @len:	Length of SG element data buffer, in bytes
 * @rsvd:	Reserved
 */
struct pds_sg_elem {
	__le64 addr;
	__le32 len;
	u8     rsvd[4];
} __packed;

/**
 * struct pds_fwctl_rpc_comp - Completion of a firmware control RPC
 * @status:     Status of the command
 * @rsvd:       Reserved
 * @comp_index: Completion index of the command
 * @err:        Error code, if any, from the RPC
 * @resp_sz:    Size of the response
 * @rsvd2:      Reserved
 * @color:      Color bit indicating the state of the completion
 */
struct pds_fwctl_rpc_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	__le32 err;
	__le32 resp_sz;
	u8     rsvd2[3];
	u8     color;
} __packed;

union pds_core_adminq_cmd {
	u8     opcode;
	u8     bytes[64];

	struct pds_core_client_reg_cmd     client_reg;
	struct pds_core_client_unreg_cmd   client_unreg;
	struct pds_core_client_request_cmd client_request;

	struct pds_core_lif_identify_cmd  lif_ident;
	struct pds_core_lif_init_cmd      lif_init;
	struct pds_core_lif_reset_cmd     lif_reset;
	struct pds_core_lif_setattr_cmd   lif_setattr;
	struct pds_core_lif_getattr_cmd   lif_getattr;

	struct pds_core_q_identify_cmd    q_ident;
	struct pds_core_q_init_cmd        q_init;

	struct pds_vdpa_cmd		  vdpa;
	struct pds_vdpa_init_cmd	  vdpa_init;
	struct pds_vdpa_ident_cmd	  vdpa_ident;
	struct pds_vdpa_status_cmd	  vdpa_status;
	struct pds_vdpa_setattr_cmd	  vdpa_setattr;
	struct pds_vdpa_set_features_cmd  vdpa_set_features;
	struct pds_vdpa_vq_init_cmd	  vdpa_vq_init;
	struct pds_vdpa_vq_reset_cmd	  vdpa_vq_reset;

	struct pds_lm_suspend_cmd	  lm_suspend;
	struct pds_lm_suspend_status_cmd  lm_suspend_status;
	struct pds_lm_resume_cmd	  lm_resume;
	struct pds_lm_state_size_cmd	  lm_state_size;
	struct pds_lm_save_cmd		  lm_save;
	struct pds_lm_restore_cmd	  lm_restore;
	struct pds_lm_host_vf_status_cmd  lm_host_vf_status;
	struct pds_lm_dirty_status_cmd	  lm_dirty_status;
	struct pds_lm_dirty_enable_cmd	  lm_dirty_enable;
	struct pds_lm_dirty_disable_cmd	  lm_dirty_disable;
	struct pds_lm_dirty_seq_ack_cmd	  lm_dirty_seq_ack;

	struct pds_fwctl_cmd		  fwctl;
	struct pds_fwctl_ident_cmd	  fwctl_ident;
	struct pds_fwctl_rpc_cmd	  fwctl_rpc;
	struct pds_fwctl_query_cmd	  fwctl_query;
};

union pds_core_adminq_comp {
	struct {
		u8     status;
		u8     rsvd;
		__le16 comp_index;
		u8     rsvd2[11];
		u8     color;
	};
	u32    words[4];

	struct pds_core_client_reg_comp   client_reg;

	struct pds_core_lif_identify_comp lif_ident;
	struct pds_core_lif_init_comp     lif_init;
	struct pds_core_lif_setattr_comp  lif_setattr;
	struct pds_core_lif_getattr_comp  lif_getattr;

	struct pds_core_q_identify_comp   q_ident;
	struct pds_core_q_init_comp       q_init;

	struct pds_vdpa_vq_init_comp	  vdpa_vq_init;
	struct pds_vdpa_vq_reset_comp	  vdpa_vq_reset;

	struct pds_lm_state_size_comp	  lm_state_size;
	struct pds_lm_dirty_status_comp	  lm_dirty_status;

	struct pds_fwctl_comp		  fwctl;
	struct pds_fwctl_rpc_comp	  fwctl_rpc;
	struct pds_fwctl_query_comp	  fwctl_query;
};

#ifndef __CHECKER__
static_assert(sizeof(union pds_core_adminq_cmd) == 64);
static_assert(sizeof(union pds_core_adminq_comp) == 16);
static_assert(sizeof(union pds_core_notifyq_comp) == 64);
#endif /* __CHECKER__ */

/* The color bit is a 'done' bit for the completion descriptors
 * where the meaning alternates between '1' and '0' for alternating
 * passes through the completion descriptor ring.
 */
static inline bool pdsc_color_match(u8 color, bool done_color)
{
	return (!!(color & PDS_COMP_COLOR_MASK)) == done_color;
}

struct pdsc;
int pdsc_adminq_post(struct pdsc *pdsc,
		     union pds_core_adminq_cmd *cmd,
		     union pds_core_adminq_comp *comp,
		     bool fast_poll);

#endif /* _PDS_CORE_ADMINQ_H_ */
