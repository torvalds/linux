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
