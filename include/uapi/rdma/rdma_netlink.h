#ifndef _UAPI_RDMA_NETLINK_H
#define _UAPI_RDMA_NETLINK_H

#include <linux/types.h>

enum {
	RDMA_NL_RDMA_CM = 1,
	RDMA_NL_IWCM,
	RDMA_NL_RSVD,
	RDMA_NL_LS,	/* RDMA Local Services */
	RDMA_NL_I40IW,
	RDMA_NL_NUM_CLIENTS
};

enum {
	RDMA_NL_GROUP_CM = 1,
	RDMA_NL_GROUP_IWPM,
	RDMA_NL_GROUP_LS,
	RDMA_NL_NUM_GROUPS
};

#define RDMA_NL_GET_CLIENT(type) ((type & (((1 << 6) - 1) << 10)) >> 10)
#define RDMA_NL_GET_OP(type) (type & ((1 << 10) - 1))
#define RDMA_NL_GET_TYPE(client, op) ((client << 10) + op)

enum {
	RDMA_NL_RDMA_CM_ID_STATS = 0,
	RDMA_NL_RDMA_CM_NUM_OPS
};

enum {
	RDMA_NL_RDMA_CM_ATTR_SRC_ADDR = 1,
	RDMA_NL_RDMA_CM_ATTR_DST_ADDR,
	RDMA_NL_RDMA_CM_NUM_ATTR,
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
	RDMA_NL_IWPM_NUM_OPS
};

struct rdma_cm_id_stats {
	__u32	qp_num;
	__u32	bound_dev_if;
	__u32	port_space;
	__s32	pid;
	__u8	cm_state;
	__u8	node_type;
	__u8	port_num;
	__u8	qp_type;
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
	IWPM_NLA_MANAGE_MAPPED_LOC_ADDR,
	IWPM_NLA_RMANAGE_MAPPING_ERR,
	IWPM_NLA_RMANAGE_MAPPING_MAX
};

#define IWPM_NLA_MANAGE_MAPPING_MAX 3
#define IWPM_NLA_QUERY_MAPPING_MAX  4
#define IWPM_NLA_MAPINFO_SEND_MAX   3

enum {
	IWPM_NLA_QUERY_MAPPING_UNSPEC = 0,
	IWPM_NLA_QUERY_MAPPING_SEQ,
	IWPM_NLA_QUERY_LOCAL_ADDR,
	IWPM_NLA_QUERY_REMOTE_ADDR,
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

/*
 * Local service operations:
 *   RESOLVE - The client requests the local service to resolve a path.
 *   SET_TIMEOUT - The local service requests the client to set the timeout.
 */
enum {
	RDMA_NL_LS_OP_RESOLVE = 0,
	RDMA_NL_LS_OP_SET_TIMEOUT,
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
	LS_NLA_TYPE_MAX
};

/* Local service DGID/SGID attribute: big endian */
struct rdma_nla_ls_gid {
	__u8		gid[16];
};

#endif /* _UAPI_RDMA_NETLINK_H */
