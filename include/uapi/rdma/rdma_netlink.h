/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_RDMA_NETLINK_H
#define _UAPI_RDMA_NETLINK_H

#include <linux/types.h>

enum {
	RDMA_NL_IWCM = 2,
	RDMA_NL_RSVD,
	RDMA_NL_LS,	/* RDMA Local Services */
	RDMA_NL_NLDEV,	/* RDMA device interface */
	RDMA_NL_NUM_CLIENTS
};

enum {
	RDMA_NL_GROUP_IWPM = 2,
	RDMA_NL_GROUP_LS,
	RDMA_NL_GROUP_NOTIFY,
	RDMA_NL_NUM_GROUPS
};

#define RDMA_NL_GET_CLIENT(type) ((type & (((1 << 6) - 1) << 10)) >> 10)
#define RDMA_NL_GET_OP(type) (type & ((1 << 10) - 1))
#define RDMA_NL_GET_TYPE(client, op) ((client << 10) + op)

/* The minimum version that the iwpm kernel supports */
#define IWPM_UABI_VERSION_MIN	3

/* The latest version that the iwpm kernel supports */
#define IWPM_UABI_VERSION	4

/* iwarp port mapper message flags */
enum {

	/* Do not map the port for this IWPM request */
	IWPM_FLAGS_NO_PORT_MAP = (1 << 0),
};

/* iwarp port mapper op-codes */
enum {
	RDMA_NL_IWPM_REG_PID = 0,
	RDMA_NL_IWPM_ADD_MAPPING,
	RDMA_NL_IWPM_QUERY_MAPPING,
	RDMA_NL_IWPM_REMOVE_MAPPING,
	RDMA_NL_IWPM_REMOTE_INFO,
	RDMA_NL_IWPM_HANDLE_ERR,
	RDMA_NL_IWPM_MAPINFO,
	RDMA_NL_IWPM_MAPINFO_NUM,
	RDMA_NL_IWPM_HELLO,
	RDMA_NL_IWPM_NUM_OPS
};

enum {
	IWPM_NLA_REG_PID_UNSPEC = 0,
	IWPM_NLA_REG_PID_SEQ,
	IWPM_NLA_REG_IF_NAME,
	IWPM_NLA_REG_IBDEV_NAME,
	IWPM_NLA_REG_ULIB_NAME,
	IWPM_NLA_REG_PID_MAX
};

enum {
	IWPM_NLA_RREG_PID_UNSPEC = 0,
	IWPM_NLA_RREG_PID_SEQ,
	IWPM_NLA_RREG_IBDEV_NAME,
	IWPM_NLA_RREG_ULIB_NAME,
	IWPM_NLA_RREG_ULIB_VER,
	IWPM_NLA_RREG_PID_ERR,
	IWPM_NLA_RREG_PID_MAX

};

enum {
	IWPM_NLA_MANAGE_MAPPING_UNSPEC = 0,
	IWPM_NLA_MANAGE_MAPPING_SEQ,
	IWPM_NLA_MANAGE_ADDR,
	IWPM_NLA_MANAGE_FLAGS,
	IWPM_NLA_MANAGE_MAPPING_MAX
};

enum {
	IWPM_NLA_RMANAGE_MAPPING_UNSPEC = 0,
	IWPM_NLA_RMANAGE_MAPPING_SEQ,
	IWPM_NLA_RMANAGE_ADDR,
	IWPM_NLA_RMANAGE_MAPPED_LOC_ADDR,
	/* The following maintains bisectability of rdma-core */
	IWPM_NLA_MANAGE_MAPPED_LOC_ADDR = IWPM_NLA_RMANAGE_MAPPED_LOC_ADDR,
	IWPM_NLA_RMANAGE_MAPPING_ERR,
	IWPM_NLA_RMANAGE_MAPPING_MAX
};

#define IWPM_NLA_MAPINFO_SEND_MAX   3
#define IWPM_NLA_REMOVE_MAPPING_MAX 3

enum {
	IWPM_NLA_QUERY_MAPPING_UNSPEC = 0,
	IWPM_NLA_QUERY_MAPPING_SEQ,
	IWPM_NLA_QUERY_LOCAL_ADDR,
	IWPM_NLA_QUERY_REMOTE_ADDR,
	IWPM_NLA_QUERY_FLAGS,
	IWPM_NLA_QUERY_MAPPING_MAX,
};

enum {
	IWPM_NLA_RQUERY_MAPPING_UNSPEC = 0,
	IWPM_NLA_RQUERY_MAPPING_SEQ,
	IWPM_NLA_RQUERY_LOCAL_ADDR,
	IWPM_NLA_RQUERY_REMOTE_ADDR,
	IWPM_NLA_RQUERY_MAPPED_LOC_ADDR,
	IWPM_NLA_RQUERY_MAPPED_REM_ADDR,
	IWPM_NLA_RQUERY_MAPPING_ERR,
	IWPM_NLA_RQUERY_MAPPING_MAX
};

enum {
	IWPM_NLA_MAPINFO_REQ_UNSPEC = 0,
	IWPM_NLA_MAPINFO_ULIB_NAME,
	IWPM_NLA_MAPINFO_ULIB_VER,
	IWPM_NLA_MAPINFO_REQ_MAX
};

enum {
	IWPM_NLA_MAPINFO_UNSPEC = 0,
	IWPM_NLA_MAPINFO_LOCAL_ADDR,
	IWPM_NLA_MAPINFO_MAPPED_ADDR,
	IWPM_NLA_MAPINFO_FLAGS,
	IWPM_NLA_MAPINFO_MAX
};

enum {
	IWPM_NLA_MAPINFO_NUM_UNSPEC = 0,
	IWPM_NLA_MAPINFO_SEQ,
	IWPM_NLA_MAPINFO_SEND_NUM,
	IWPM_NLA_MAPINFO_ACK_NUM,
	IWPM_NLA_MAPINFO_NUM_MAX
};

enum {
	IWPM_NLA_ERR_UNSPEC = 0,
	IWPM_NLA_ERR_SEQ,
	IWPM_NLA_ERR_CODE,
	IWPM_NLA_ERR_MAX
};

enum {
	IWPM_NLA_HELLO_UNSPEC = 0,
	IWPM_NLA_HELLO_ABI_VERSION,
	IWPM_NLA_HELLO_MAX
};

/* For RDMA_NLDEV_ATTR_DEV_NODE_TYPE */
enum {
	/* IB values map to NodeInfo:NodeType. */
	RDMA_NODE_IB_CA = 1,
	RDMA_NODE_IB_SWITCH,
	RDMA_NODE_IB_ROUTER,
	RDMA_NODE_RNIC,
	RDMA_NODE_USNIC,
	RDMA_NODE_USNIC_UDP,
	RDMA_NODE_UNSPECIFIED,
};

/*
 * Local service operations:
 *   RESOLVE - The client requests the local service to resolve a path.
 *   SET_TIMEOUT - The local service requests the client to set the timeout.
 *   IP_RESOLVE - The client requests the local service to resolve an IP to GID.
 */
enum {
	RDMA_NL_LS_OP_RESOLVE = 0,
	RDMA_NL_LS_OP_SET_TIMEOUT,
	RDMA_NL_LS_OP_IP_RESOLVE,
	RDMA_NL_LS_NUM_OPS
};

/* Local service netlink message flags */
#define RDMA_NL_LS_F_ERR	0x0100	/* Failed response */

/*
 * Local service resolve operation family header.
 * The layout for the resolve operation:
 *    nlmsg header
 *    family header
 *    attributes
 */

/*
 * Local service path use:
 * Specify how the path(s) will be used.
 *   ALL - For connected CM operation (6 pathrecords)
 *   UNIDIRECTIONAL - For unidirectional UD (1 pathrecord)
 *   GMP - For miscellaneous GMP like operation (at least 1 reversible
 *         pathrecord)
 */
enum {
	LS_RESOLVE_PATH_USE_ALL = 0,
	LS_RESOLVE_PATH_USE_UNIDIRECTIONAL,
	LS_RESOLVE_PATH_USE_GMP,
	LS_RESOLVE_PATH_USE_MAX
};

#define LS_DEVICE_NAME_MAX 64

struct rdma_ls_resolve_header {
	__u8 device_name[LS_DEVICE_NAME_MAX];
	__u8 port_num;
	__u8 path_use;
};

struct rdma_ls_ip_resolve_header {
	__u32 ifindex;
};

/* Local service attribute type */
#define RDMA_NLA_F_MANDATORY	(1 << 13)
#define RDMA_NLA_TYPE_MASK	(~(NLA_F_NESTED | NLA_F_NET_BYTEORDER | \
				  RDMA_NLA_F_MANDATORY))

/*
 * Local service attributes:
 *   Attr Name       Size                       Byte order
 *   -----------------------------------------------------
 *   PATH_RECORD     struct ib_path_rec_data
 *   TIMEOUT         u32                        cpu
 *   SERVICE_ID      u64                        cpu
 *   DGID            u8[16]                     BE
 *   SGID            u8[16]                     BE
 *   TCLASS          u8
 *   PKEY            u16                        cpu
 *   QOS_CLASS       u16                        cpu
 *   IPV4            u32                        BE
 *   IPV6            u8[16]                     BE
 */
enum {
	LS_NLA_TYPE_UNSPEC = 0,
	LS_NLA_TYPE_PATH_RECORD,
	LS_NLA_TYPE_TIMEOUT,
	LS_NLA_TYPE_SERVICE_ID,
	LS_NLA_TYPE_DGID,
	LS_NLA_TYPE_SGID,
	LS_NLA_TYPE_TCLASS,
	LS_NLA_TYPE_PKEY,
	LS_NLA_TYPE_QOS_CLASS,
	LS_NLA_TYPE_IPV4,
	LS_NLA_TYPE_IPV6,
	LS_NLA_TYPE_MAX
};

/* Local service DGID/SGID attribute: big endian */
struct rdma_nla_ls_gid {
	__u8		gid[16];
};

enum rdma_nldev_command {
	RDMA_NLDEV_CMD_UNSPEC,

	RDMA_NLDEV_CMD_GET, /* can dump */
	RDMA_NLDEV_CMD_SET,

	RDMA_NLDEV_CMD_NEWLINK,

	RDMA_NLDEV_CMD_DELLINK,

	RDMA_NLDEV_CMD_PORT_GET, /* can dump */

	RDMA_NLDEV_CMD_SYS_GET,
	RDMA_NLDEV_CMD_SYS_SET,

	/* 8 is free to use */

	RDMA_NLDEV_CMD_RES_GET = 9, /* can dump */

	RDMA_NLDEV_CMD_RES_QP_GET, /* can dump */

	RDMA_NLDEV_CMD_RES_CM_ID_GET, /* can dump */

	RDMA_NLDEV_CMD_RES_CQ_GET, /* can dump */

	RDMA_NLDEV_CMD_RES_MR_GET, /* can dump */

	RDMA_NLDEV_CMD_RES_PD_GET, /* can dump */

	RDMA_NLDEV_CMD_GET_CHARDEV,

	RDMA_NLDEV_CMD_STAT_SET,

	RDMA_NLDEV_CMD_STAT_GET, /* can dump */

	RDMA_NLDEV_CMD_STAT_DEL,

	RDMA_NLDEV_CMD_RES_QP_GET_RAW,

	RDMA_NLDEV_CMD_RES_CQ_GET_RAW,

	RDMA_NLDEV_CMD_RES_MR_GET_RAW,

	RDMA_NLDEV_CMD_RES_CTX_GET, /* can dump */

	RDMA_NLDEV_CMD_RES_SRQ_GET, /* can dump */

	RDMA_NLDEV_CMD_STAT_GET_STATUS,

	RDMA_NLDEV_CMD_RES_SRQ_GET_RAW,

	RDMA_NLDEV_CMD_NEWDEV,

	RDMA_NLDEV_CMD_DELDEV,

	RDMA_NLDEV_CMD_MONITOR,

	RDMA_NLDEV_NUM_OPS
};

enum rdma_nldev_print_type {
	RDMA_NLDEV_PRINT_TYPE_UNSPEC,
	RDMA_NLDEV_PRINT_TYPE_HEX,
};

enum rdma_nldev_attr {
	/* don't change the order or add anything between, this is ABI! */
	RDMA_NLDEV_ATTR_UNSPEC,

	/* Pad attribute for 64b alignment */
	RDMA_NLDEV_ATTR_PAD = RDMA_NLDEV_ATTR_UNSPEC,

	/* Identifier for ib_device */
	RDMA_NLDEV_ATTR_DEV_INDEX,		/* u32 */

	RDMA_NLDEV_ATTR_DEV_NAME,		/* string */
	/*
	 * Device index together with port index are identifiers
	 * for port/link properties.
	 *
	 * For RDMA_NLDEV_CMD_GET commamnd, port index will return number
	 * of available ports in ib_device, while for port specific operations,
	 * it will be real port index as it appears in sysfs. Port index follows
	 * sysfs notation and starts from 1 for the first port.
	 */
	RDMA_NLDEV_ATTR_PORT_INDEX,		/* u32 */

	/*
	 * Device and port capabilities
	 *
	 * When used for port info, first 32-bits are CapabilityMask followed by
	 * 16-bit CapabilityMask2.
	 */
	RDMA_NLDEV_ATTR_CAP_FLAGS,		/* u64 */

	/*
	 * FW version
	 */
	RDMA_NLDEV_ATTR_FW_VERSION,		/* string */

	/*
	 * Node GUID (in host byte order) associated with the RDMA device.
	 */
	RDMA_NLDEV_ATTR_NODE_GUID,			/* u64 */

	/*
	 * System image GUID (in host byte order) associated with
	 * this RDMA device and other devices which are part of a
	 * single system.
	 */
	RDMA_NLDEV_ATTR_SYS_IMAGE_GUID,		/* u64 */

	/*
	 * Subnet prefix (in host byte order)
	 */
	RDMA_NLDEV_ATTR_SUBNET_PREFIX,		/* u64 */

	/*
	 * Local Identifier (LID),
	 * According to IB specification, It is 16-bit address assigned
	 * by the Subnet Manager. Extended to be 32-bit for OmniPath users.
	 */
	RDMA_NLDEV_ATTR_LID,			/* u32 */
	RDMA_NLDEV_ATTR_SM_LID,			/* u32 */

	/*
	 * LID mask control (LMC)
	 */
	RDMA_NLDEV_ATTR_LMC,			/* u8 */

	RDMA_NLDEV_ATTR_PORT_STATE,		/* u8 */
	RDMA_NLDEV_ATTR_PORT_PHYS_STATE,	/* u8 */

	RDMA_NLDEV_ATTR_DEV_NODE_TYPE,		/* u8 */

	RDMA_NLDEV_ATTR_RES_SUMMARY,		/* nested table */
	RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY,	/* nested table */
	RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_NAME,	/* string */
	RDMA_NLDEV_ATTR_RES_SUMMARY_ENTRY_CURR,	/* u64 */

	RDMA_NLDEV_ATTR_RES_QP,			/* nested table */
	RDMA_NLDEV_ATTR_RES_QP_ENTRY,		/* nested table */
	/*
	 * Local QPN
	 */
	RDMA_NLDEV_ATTR_RES_LQPN,		/* u32 */
	/*
	 * Remote QPN,
	 * Applicable for RC and UC only IBTA 11.2.5.3 QUERY QUEUE PAIR
	 */
	RDMA_NLDEV_ATTR_RES_RQPN,		/* u32 */
	/*
	 * Receive Queue PSN,
	 * Applicable for RC and UC only 11.2.5.3 QUERY QUEUE PAIR
	 */
	RDMA_NLDEV_ATTR_RES_RQ_PSN,		/* u32 */
	/*
	 * Send Queue PSN
	 */
	RDMA_NLDEV_ATTR_RES_SQ_PSN,		/* u32 */
	RDMA_NLDEV_ATTR_RES_PATH_MIG_STATE,	/* u8 */
	/*
	 * QP types as visible to RDMA/core, the reserved QPT
	 * are not exported through this interface.
	 */
	RDMA_NLDEV_ATTR_RES_TYPE,		/* u8 */
	RDMA_NLDEV_ATTR_RES_STATE,		/* u8 */
	/*
	 * Process ID which created object,
	 * in case of kernel origin, PID won't exist.
	 */
	RDMA_NLDEV_ATTR_RES_PID,		/* u32 */
	/*
	 * The name of process created following resource.
	 * It will exist only for kernel objects.
	 * For user created objects, the user is supposed
	 * to read /proc/PID/comm file.
	 */
	RDMA_NLDEV_ATTR_RES_KERN_NAME,		/* string */

	RDMA_NLDEV_ATTR_RES_CM_ID,		/* nested table */
	RDMA_NLDEV_ATTR_RES_CM_ID_ENTRY,	/* nested table */
	/*
	 * rdma_cm_id port space.
	 */
	RDMA_NLDEV_ATTR_RES_PS,			/* u32 */
	/*
	 * Source and destination socket addresses
	 */
	RDMA_NLDEV_ATTR_RES_SRC_ADDR,		/* __kernel_sockaddr_storage */
	RDMA_NLDEV_ATTR_RES_DST_ADDR,		/* __kernel_sockaddr_storage */

	RDMA_NLDEV_ATTR_RES_CQ,			/* nested table */
	RDMA_NLDEV_ATTR_RES_CQ_ENTRY,		/* nested table */
	RDMA_NLDEV_ATTR_RES_CQE,		/* u32 */
	RDMA_NLDEV_ATTR_RES_USECNT,		/* u64 */
	RDMA_NLDEV_ATTR_RES_POLL_CTX,		/* u8 */

	RDMA_NLDEV_ATTR_RES_MR,			/* nested table */
	RDMA_NLDEV_ATTR_RES_MR_ENTRY,		/* nested table */
	RDMA_NLDEV_ATTR_RES_RKEY,		/* u32 */
	RDMA_NLDEV_ATTR_RES_LKEY,		/* u32 */
	RDMA_NLDEV_ATTR_RES_IOVA,		/* u64 */
	RDMA_NLDEV_ATTR_RES_MRLEN,		/* u64 */

	RDMA_NLDEV_ATTR_RES_PD,			/* nested table */
	RDMA_NLDEV_ATTR_RES_PD_ENTRY,		/* nested table */
	RDMA_NLDEV_ATTR_RES_LOCAL_DMA_LKEY,	/* u32 */
	RDMA_NLDEV_ATTR_RES_UNSAFE_GLOBAL_RKEY,	/* u32 */
	/*
	 * Provides logical name and index of netdevice which is
	 * connected to physical port. This information is relevant
	 * for RoCE and iWARP.
	 *
	 * The netdevices which are associated with containers are
	 * supposed to be exported together with GID table once it
	 * will be exposed through the netlink. Because the
	 * associated netdevices are properties of GIDs.
	 */
	RDMA_NLDEV_ATTR_NDEV_INDEX,		/* u32 */
	RDMA_NLDEV_ATTR_NDEV_NAME,		/* string */
	/*
	 * driver-specific attributes.
	 */
	RDMA_NLDEV_ATTR_DRIVER,			/* nested table */
	RDMA_NLDEV_ATTR_DRIVER_ENTRY,		/* nested table */
	RDMA_NLDEV_ATTR_DRIVER_STRING,		/* string */
	/*
	 * u8 values from enum rdma_nldev_print_type
	 */
	RDMA_NLDEV_ATTR_DRIVER_PRINT_TYPE,	/* u8 */
	RDMA_NLDEV_ATTR_DRIVER_S32,		/* s32 */
	RDMA_NLDEV_ATTR_DRIVER_U32,		/* u32 */
	RDMA_NLDEV_ATTR_DRIVER_S64,		/* s64 */
	RDMA_NLDEV_ATTR_DRIVER_U64,		/* u64 */

	/*
	 * Indexes to get/set secific entry,
	 * for QP use RDMA_NLDEV_ATTR_RES_LQPN
	 */
	RDMA_NLDEV_ATTR_RES_PDN,               /* u32 */
	RDMA_NLDEV_ATTR_RES_CQN,               /* u32 */
	RDMA_NLDEV_ATTR_RES_MRN,               /* u32 */
	RDMA_NLDEV_ATTR_RES_CM_IDN,            /* u32 */
	RDMA_NLDEV_ATTR_RES_CTXN,	       /* u32 */
	/*
	 * Identifies the rdma driver. eg: "rxe" or "siw"
	 */
	RDMA_NLDEV_ATTR_LINK_TYPE,		/* string */

	/*
	 * net namespace mode for rdma subsystem:
	 * either shared or exclusive among multiple net namespaces.
	 */
	RDMA_NLDEV_SYS_ATTR_NETNS_MODE,		/* u8 */
	/*
	 * Device protocol, e.g. ib, iw, usnic, roce and opa
	 */
	RDMA_NLDEV_ATTR_DEV_PROTOCOL,		/* string */

	/*
	 * File descriptor handle of the net namespace object
	 */
	RDMA_NLDEV_NET_NS_FD,			/* u32 */
	/*
	 * Information about a chardev.
	 * CHARDEV_TYPE is the name of the chardev ABI (ie uverbs, umad, etc)
	 * CHARDEV_ABI signals the ABI revision (historical)
	 * CHARDEV_NAME is the kernel name for the /dev/ file (no directory)
	 * CHARDEV is the 64 bit dev_t for the inode
	 */
	RDMA_NLDEV_ATTR_CHARDEV_TYPE,		/* string */
	RDMA_NLDEV_ATTR_CHARDEV_NAME,		/* string */
	RDMA_NLDEV_ATTR_CHARDEV_ABI,		/* u64 */
	RDMA_NLDEV_ATTR_CHARDEV,		/* u64 */
	RDMA_NLDEV_ATTR_UVERBS_DRIVER_ID,       /* u64 */
	/*
	 * Counter-specific attributes.
	 */
	RDMA_NLDEV_ATTR_STAT_MODE,		/* u32 */
	RDMA_NLDEV_ATTR_STAT_RES,		/* u32 */
	RDMA_NLDEV_ATTR_STAT_AUTO_MODE_MASK,	/* u32 */
	RDMA_NLDEV_ATTR_STAT_COUNTER,		/* nested table */
	RDMA_NLDEV_ATTR_STAT_COUNTER_ENTRY,	/* nested table */
	RDMA_NLDEV_ATTR_STAT_COUNTER_ID,	/* u32 */
	RDMA_NLDEV_ATTR_STAT_HWCOUNTERS,	/* nested table */
	RDMA_NLDEV_ATTR_STAT_HWCOUNTER_ENTRY,	/* nested table */
	RDMA_NLDEV_ATTR_STAT_HWCOUNTER_ENTRY_NAME,	/* string */
	RDMA_NLDEV_ATTR_STAT_HWCOUNTER_ENTRY_VALUE,	/* u64 */

	/*
	 * CQ adaptive moderatio (DIM)
	 */
	RDMA_NLDEV_ATTR_DEV_DIM,                /* u8 */

	RDMA_NLDEV_ATTR_RES_RAW,	/* binary */

	RDMA_NLDEV_ATTR_RES_CTX,		/* nested table */
	RDMA_NLDEV_ATTR_RES_CTX_ENTRY,		/* nested table */

	RDMA_NLDEV_ATTR_RES_SRQ,		/* nested table */
	RDMA_NLDEV_ATTR_RES_SRQ_ENTRY,		/* nested table */
	RDMA_NLDEV_ATTR_RES_SRQN,		/* u32 */

	RDMA_NLDEV_ATTR_MIN_RANGE,		/* u32 */
	RDMA_NLDEV_ATTR_MAX_RANGE,		/* u32 */

	RDMA_NLDEV_SYS_ATTR_COPY_ON_FORK,	/* u8 */

	RDMA_NLDEV_ATTR_STAT_HWCOUNTER_INDEX,	/* u32 */
	RDMA_NLDEV_ATTR_STAT_HWCOUNTER_DYNAMIC, /* u8 */

	RDMA_NLDEV_SYS_ATTR_PRIVILEGED_QKEY_MODE, /* u8 */

	RDMA_NLDEV_ATTR_DRIVER_DETAILS,		/* u8 */
	/*
	 * QP subtype string, used for driver QPs
	 */
	RDMA_NLDEV_ATTR_RES_SUBTYPE,		/* string */

	RDMA_NLDEV_ATTR_DEV_TYPE,		/* u8 */

	RDMA_NLDEV_ATTR_PARENT_NAME,		/* string */

	RDMA_NLDEV_ATTR_NAME_ASSIGN_TYPE,	/* u8 */

	RDMA_NLDEV_ATTR_EVENT_TYPE,		/* u8 */

	RDMA_NLDEV_SYS_ATTR_MONITOR_MODE,	/* u8 */
	/*
	 * Always the end
	 */
	RDMA_NLDEV_ATTR_MAX
};

/*
 * Supported counter bind modes. All modes are mutual-exclusive.
 */
enum rdma_nl_counter_mode {
	RDMA_COUNTER_MODE_NONE,

	/*
	 * A qp is bound with a counter automatically during initialization
	 * based on the auto mode (e.g., qp type, ...)
	 */
	RDMA_COUNTER_MODE_AUTO,

	/*
	 * Which qp are bound with which counter is explicitly specified
	 * by the user
	 */
	RDMA_COUNTER_MODE_MANUAL,

	/*
	 * Always the end
	 */
	RDMA_COUNTER_MODE_MAX,
};

/*
 * Supported criteria in counter auto mode.
 * Currently only "qp type" is supported
 */
enum rdma_nl_counter_mask {
	RDMA_COUNTER_MASK_QP_TYPE = 1,
	RDMA_COUNTER_MASK_PID = 1 << 1,
};

/* Supported rdma device types. */
enum rdma_nl_dev_type {
	RDMA_DEVICE_TYPE_SMI = 1,
};

/* RDMA device name assignment types */
enum rdma_nl_name_assign_type {
	RDMA_NAME_ASSIGN_TYPE_UNKNOWN = 0,
	RDMA_NAME_ASSIGN_TYPE_USER = 1, /* Provided by user-space */
};

/*
 * Supported rdma monitoring event types.
 */
enum rdma_nl_notify_event_type {
	RDMA_REGISTER_EVENT,
	RDMA_UNREGISTER_EVENT,
	RDMA_NETDEV_ATTACH_EVENT,
	RDMA_NETDEV_DETACH_EVENT,
	RDMA_RENAME_EVENT,
	RDMA_NETDEV_RENAME_EVENT,
};

#endif /* _UAPI_RDMA_NETLINK_H */
