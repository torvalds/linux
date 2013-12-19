/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc.  All rights reserved.
 * Copyright (c) 2006 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_SA_H
#define IB_SA_H

#include <linux/completion.h>
#include <linux/compiler.h>

#include <linux/atomic.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>

enum {
	IB_SA_CLASS_VERSION		= 2,	/* IB spec version 1.1/1.2 */

	IB_SA_METHOD_GET_TABLE		= 0x12,
	IB_SA_METHOD_GET_TABLE_RESP	= 0x92,
	IB_SA_METHOD_DELETE		= 0x15,
	IB_SA_METHOD_DELETE_RESP	= 0x95,
	IB_SA_METHOD_GET_MULTI		= 0x14,
	IB_SA_METHOD_GET_MULTI_RESP	= 0x94,
	IB_SA_METHOD_GET_TRACE_TBL	= 0x13
};

enum {
	IB_SA_ATTR_CLASS_PORTINFO    = 0x01,
	IB_SA_ATTR_NOTICE	     = 0x02,
	IB_SA_ATTR_INFORM_INFO	     = 0x03,
	IB_SA_ATTR_NODE_REC	     = 0x11,
	IB_SA_ATTR_PORT_INFO_REC     = 0x12,
	IB_SA_ATTR_SL2VL_REC	     = 0x13,
	IB_SA_ATTR_SWITCH_REC	     = 0x14,
	IB_SA_ATTR_LINEAR_FDB_REC    = 0x15,
	IB_SA_ATTR_RANDOM_FDB_REC    = 0x16,
	IB_SA_ATTR_MCAST_FDB_REC     = 0x17,
	IB_SA_ATTR_SM_INFO_REC	     = 0x18,
	IB_SA_ATTR_LINK_REC	     = 0x20,
	IB_SA_ATTR_GUID_INFO_REC     = 0x30,
	IB_SA_ATTR_SERVICE_REC	     = 0x31,
	IB_SA_ATTR_PARTITION_REC     = 0x33,
	IB_SA_ATTR_PATH_REC	     = 0x35,
	IB_SA_ATTR_VL_ARB_REC	     = 0x36,
	IB_SA_ATTR_MC_MEMBER_REC     = 0x38,
	IB_SA_ATTR_TRACE_REC	     = 0x39,
	IB_SA_ATTR_MULTI_PATH_REC    = 0x3a,
	IB_SA_ATTR_SERVICE_ASSOC_REC = 0x3b,
	IB_SA_ATTR_INFORM_INFO_REC   = 0xf3
};

enum ib_sa_selector {
	IB_SA_GT   = 0,
	IB_SA_LT   = 1,
	IB_SA_EQ   = 2,
	/*
	 * The meaning of "best" depends on the attribute: for
	 * example, for MTU best will return the largest available
	 * MTU, while for packet life time, best will return the
	 * smallest available life time.
	 */
	IB_SA_BEST = 3
};

/*
 * Structures for SA records are named "struct ib_sa_xxx_rec."  No
 * attempt is made to pack structures to match the physical layout of
 * SA records in SA MADs; all packing and unpacking is handled by the
 * SA query code.
 *
 * For a record with structure ib_sa_xxx_rec, the naming convention
 * for the component mask value for field yyy is IB_SA_XXX_REC_YYY (we
 * never use different abbreviations or otherwise change the spelling
 * of xxx/yyy between ib_sa_xxx_rec.yyy and IB_SA_XXX_REC_YYY).
 *
 * Reserved rows are indicated with comments to help maintainability.
 */

#define IB_SA_PATH_REC_SERVICE_ID		       (IB_SA_COMP_MASK( 0) |\
							IB_SA_COMP_MASK( 1))
#define IB_SA_PATH_REC_DGID				IB_SA_COMP_MASK( 2)
#define IB_SA_PATH_REC_SGID				IB_SA_COMP_MASK( 3)
#define IB_SA_PATH_REC_DLID				IB_SA_COMP_MASK( 4)
#define IB_SA_PATH_REC_SLID				IB_SA_COMP_MASK( 5)
#define IB_SA_PATH_REC_RAW_TRAFFIC			IB_SA_COMP_MASK( 6)
/* reserved:								 7 */
#define IB_SA_PATH_REC_FLOW_LABEL       		IB_SA_COMP_MASK( 8)
#define IB_SA_PATH_REC_HOP_LIMIT			IB_SA_COMP_MASK( 9)
#define IB_SA_PATH_REC_TRAFFIC_CLASS			IB_SA_COMP_MASK(10)
#define IB_SA_PATH_REC_REVERSIBLE			IB_SA_COMP_MASK(11)
#define IB_SA_PATH_REC_NUMB_PATH			IB_SA_COMP_MASK(12)
#define IB_SA_PATH_REC_PKEY				IB_SA_COMP_MASK(13)
#define IB_SA_PATH_REC_QOS_CLASS			IB_SA_COMP_MASK(14)
#define IB_SA_PATH_REC_SL				IB_SA_COMP_MASK(15)
#define IB_SA_PATH_REC_MTU_SELECTOR			IB_SA_COMP_MASK(16)
#define IB_SA_PATH_REC_MTU				IB_SA_COMP_MASK(17)
#define IB_SA_PATH_REC_RATE_SELECTOR			IB_SA_COMP_MASK(18)
#define IB_SA_PATH_REC_RATE				IB_SA_COMP_MASK(19)
#define IB_SA_PATH_REC_PACKET_LIFE_TIME_SELECTOR	IB_SA_COMP_MASK(20)
#define IB_SA_PATH_REC_PACKET_LIFE_TIME			IB_SA_COMP_MASK(21)
#define IB_SA_PATH_REC_PREFERENCE			IB_SA_COMP_MASK(22)

struct ib_sa_path_rec {
	__be64       service_id;
	union ib_gid dgid;
	union ib_gid sgid;
	__be16       dlid;
	__be16       slid;
	int          raw_traffic;
	/* reserved */
	__be32       flow_label;
	u8           hop_limit;
	u8           traffic_class;
	int          reversible;
	u8           numb_path;
	__be16       pkey;
	__be16       qos_class;
	u8           sl;
	u8           mtu_selector;
	u8           mtu;
	u8           rate_selector;
	u8           rate;
	u8           packet_life_time_selector;
	u8           packet_life_time;
	u8           preference;
};

#define IB_SA_MCMEMBER_REC_MGID				IB_SA_COMP_MASK( 0)
#define IB_SA_MCMEMBER_REC_PORT_GID			IB_SA_COMP_MASK( 1)
#define IB_SA_MCMEMBER_REC_QKEY				IB_SA_COMP_MASK( 2)
#define IB_SA_MCMEMBER_REC_MLID				IB_SA_COMP_MASK( 3)
#define IB_SA_MCMEMBER_REC_MTU_SELECTOR			IB_SA_COMP_MASK( 4)
#define IB_SA_MCMEMBER_REC_MTU				IB_SA_COMP_MASK( 5)
#define IB_SA_MCMEMBER_REC_TRAFFIC_CLASS		IB_SA_COMP_MASK( 6)
#define IB_SA_MCMEMBER_REC_PKEY				IB_SA_COMP_MASK( 7)
#define IB_SA_MCMEMBER_REC_RATE_SELECTOR		IB_SA_COMP_MASK( 8)
#define IB_SA_MCMEMBER_REC_RATE				IB_SA_COMP_MASK( 9)
#define IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME_SELECTOR	IB_SA_COMP_MASK(10)
#define IB_SA_MCMEMBER_REC_PACKET_LIFE_TIME		IB_SA_COMP_MASK(11)
#define IB_SA_MCMEMBER_REC_SL				IB_SA_COMP_MASK(12)
#define IB_SA_MCMEMBER_REC_FLOW_LABEL			IB_SA_COMP_MASK(13)
#define IB_SA_MCMEMBER_REC_HOP_LIMIT			IB_SA_COMP_MASK(14)
#define IB_SA_MCMEMBER_REC_SCOPE			IB_SA_COMP_MASK(15)
#define IB_SA_MCMEMBER_REC_JOIN_STATE			IB_SA_COMP_MASK(16)
#define IB_SA_MCMEMBER_REC_PROXY_JOIN			IB_SA_COMP_MASK(17)

struct ib_sa_mcmember_rec {
	union ib_gid mgid;
	union ib_gid port_gid;
	__be32       qkey;
	__be16       mlid;
	u8           mtu_selector;
	u8           mtu;
	u8           traffic_class;
	__be16       pkey;
	u8 	     rate_selector;
	u8 	     rate;
	u8 	     packet_life_time_selector;
	u8 	     packet_life_time;
	u8           sl;
	__be32       flow_label;
	u8           hop_limit;
	u8           scope;
	u8           join_state;
	int          proxy_join;
};

/* Service Record Component Mask Sec 15.2.5.14 Ver 1.1	*/
#define IB_SA_SERVICE_REC_SERVICE_ID			IB_SA_COMP_MASK( 0)
#define IB_SA_SERVICE_REC_SERVICE_GID			IB_SA_COMP_MASK( 1)
#define IB_SA_SERVICE_REC_SERVICE_PKEY			IB_SA_COMP_MASK( 2)
/* reserved:								 3 */
#define IB_SA_SERVICE_REC_SERVICE_LEASE			IB_SA_COMP_MASK( 4)
#define IB_SA_SERVICE_REC_SERVICE_KEY			IB_SA_COMP_MASK( 5)
#define IB_SA_SERVICE_REC_SERVICE_NAME			IB_SA_COMP_MASK( 6)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_0		IB_SA_COMP_MASK( 7)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_1		IB_SA_COMP_MASK( 8)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_2		IB_SA_COMP_MASK( 9)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_3		IB_SA_COMP_MASK(10)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_4		IB_SA_COMP_MASK(11)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_5		IB_SA_COMP_MASK(12)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_6		IB_SA_COMP_MASK(13)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_7		IB_SA_COMP_MASK(14)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_8		IB_SA_COMP_MASK(15)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_9		IB_SA_COMP_MASK(16)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_10		IB_SA_COMP_MASK(17)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_11		IB_SA_COMP_MASK(18)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_12		IB_SA_COMP_MASK(19)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_13		IB_SA_COMP_MASK(20)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_14		IB_SA_COMP_MASK(21)
#define IB_SA_SERVICE_REC_SERVICE_DATA8_15		IB_SA_COMP_MASK(22)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_0		IB_SA_COMP_MASK(23)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_1		IB_SA_COMP_MASK(24)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_2		IB_SA_COMP_MASK(25)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_3		IB_SA_COMP_MASK(26)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_4		IB_SA_COMP_MASK(27)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_5		IB_SA_COMP_MASK(28)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_6		IB_SA_COMP_MASK(29)
#define IB_SA_SERVICE_REC_SERVICE_DATA16_7		IB_SA_COMP_MASK(30)
#define IB_SA_SERVICE_REC_SERVICE_DATA32_0		IB_SA_COMP_MASK(31)
#define IB_SA_SERVICE_REC_SERVICE_DATA32_1		IB_SA_COMP_MASK(32)
#define IB_SA_SERVICE_REC_SERVICE_DATA32_2		IB_SA_COMP_MASK(33)
#define IB_SA_SERVICE_REC_SERVICE_DATA32_3		IB_SA_COMP_MASK(34)
#define IB_SA_SERVICE_REC_SERVICE_DATA64_0		IB_SA_COMP_MASK(35)
#define IB_SA_SERVICE_REC_SERVICE_DATA64_1		IB_SA_COMP_MASK(36)

#define IB_DEFAULT_SERVICE_LEASE 	0xFFFFFFFF

struct ib_sa_service_rec {
	u64		id;
	union ib_gid	gid;
	__be16 		pkey;
	/* reserved */
	u32		lease;
	u8		key[16];
	u8		name[64];
	u8		data8[16];
	u16		data16[8];
	u32		data32[4];
	u64		data64[2];
};

#define IB_SA_GUIDINFO_REC_LID		IB_SA_COMP_MASK(0)
#define IB_SA_GUIDINFO_REC_BLOCK_NUM	IB_SA_COMP_MASK(1)
#define IB_SA_GUIDINFO_REC_RES1		IB_SA_COMP_MASK(2)
#define IB_SA_GUIDINFO_REC_RES2		IB_SA_COMP_MASK(3)
#define IB_SA_GUIDINFO_REC_GID0		IB_SA_COMP_MASK(4)
#define IB_SA_GUIDINFO_REC_GID1		IB_SA_COMP_MASK(5)
#define IB_SA_GUIDINFO_REC_GID2		IB_SA_COMP_MASK(6)
#define IB_SA_GUIDINFO_REC_GID3		IB_SA_COMP_MASK(7)
#define IB_SA_GUIDINFO_REC_GID4		IB_SA_COMP_MASK(8)
#define IB_SA_GUIDINFO_REC_GID5		IB_SA_COMP_MASK(9)
#define IB_SA_GUIDINFO_REC_GID6		IB_SA_COMP_MASK(10)
#define IB_SA_GUIDINFO_REC_GID7		IB_SA_COMP_MASK(11)

struct ib_sa_guidinfo_rec {
	__be16	lid;
	u8	block_num;
	/* reserved */
	u8	res1;
	__be32	res2;
	u8	guid_info_list[64];
};

struct ib_sa_client {
	atomic_t users;
	struct completion comp;
};

/**
 * ib_sa_register_client - Register an SA client.
 */
void ib_sa_register_client(struct ib_sa_client *client);

/**
 * ib_sa_unregister_client - Deregister an SA client.
 * @client: Client object to deregister.
 */
void ib_sa_unregister_client(struct ib_sa_client *client);

struct ib_sa_query;

void ib_sa_cancel_query(int id, struct ib_sa_query *query);

int ib_sa_path_rec_get(struct ib_sa_client *client,
		       struct ib_device *device, u8 port_num,
		       struct ib_sa_path_rec *rec,
		       ib_sa_comp_mask comp_mask,
		       int timeout_ms, gfp_t gfp_mask,
		       void (*callback)(int status,
					struct ib_sa_path_rec *resp,
					void *context),
		       void *context,
		       struct ib_sa_query **query);

int ib_sa_service_rec_query(struct ib_sa_client *client,
			 struct ib_device *device, u8 port_num,
			 u8 method,
			 struct ib_sa_service_rec *rec,
			 ib_sa_comp_mask comp_mask,
			 int timeout_ms, gfp_t gfp_mask,
			 void (*callback)(int status,
					  struct ib_sa_service_rec *resp,
					  void *context),
			 void *context,
			 struct ib_sa_query **sa_query);

struct ib_sa_multicast {
	struct ib_sa_mcmember_rec rec;
	ib_sa_comp_mask		comp_mask;
	int			(*callback)(int status,
					    struct ib_sa_multicast *multicast);
	void			*context;
};

/**
 * ib_sa_join_multicast - Initiates a join request to the specified multicast
 *   group.
 * @client: SA client
 * @device: Device associated with the multicast group.
 * @port_num: Port on the specified device to associate with the multicast
 *   group.
 * @rec: SA multicast member record specifying group attributes.
 * @comp_mask: Component mask indicating which group attributes of %rec are
 *   valid.
 * @gfp_mask: GFP mask for memory allocations.
 * @callback: User callback invoked once the join operation completes.
 * @context: User specified context stored with the ib_sa_multicast structure.
 *
 * This call initiates a multicast join request with the SA for the specified
 * multicast group.  If the join operation is started successfully, it returns
 * an ib_sa_multicast structure that is used to track the multicast operation.
 * Users must free this structure by calling ib_free_multicast, even if the
 * join operation later fails.  (The callback status is non-zero.)
 *
 * If the join operation fails; status will be non-zero, with the following
 * failures possible:
 * -ETIMEDOUT: The request timed out.
 * -EIO: An error occurred sending the query.
 * -EINVAL: The MCMemberRecord values differed from the existing group's.
 * -ENETRESET: Indicates that an fatal error has occurred on the multicast
 *   group, and the user must rejoin the group to continue using it.
 */
struct ib_sa_multicast *ib_sa_join_multicast(struct ib_sa_client *client,
					     struct ib_device *device, u8 port_num,
					     struct ib_sa_mcmember_rec *rec,
					     ib_sa_comp_mask comp_mask, gfp_t gfp_mask,
					     int (*callback)(int status,
							     struct ib_sa_multicast
								    *multicast),
					     void *context);

/**
 * ib_free_multicast - Frees the multicast tracking structure, and releases
 *    any reference on the multicast group.
 * @multicast: Multicast tracking structure allocated by ib_join_multicast.
 *
 * This call blocks until the multicast identifier is destroyed.  It may
 * not be called from within the multicast callback; however, returning a non-
 * zero value from the callback will result in destroying the multicast
 * tracking structure.
 */
void ib_sa_free_multicast(struct ib_sa_multicast *multicast);

/**
 * ib_get_mcmember_rec - Looks up a multicast member record by its MGID and
 *   returns it if found.
 * @device: Device associated with the multicast group.
 * @port_num: Port on the specified device to associate with the multicast
 *   group.
 * @mgid: MGID of multicast group.
 * @rec: Location to copy SA multicast member record.
 */
int ib_sa_get_mcmember_rec(struct ib_device *device, u8 port_num,
			   union ib_gid *mgid, struct ib_sa_mcmember_rec *rec);

/**
 * ib_init_ah_from_mcmember - Initialize address handle attributes based on
 * an SA multicast member record.
 */
int ib_init_ah_from_mcmember(struct ib_device *device, u8 port_num,
			     struct ib_sa_mcmember_rec *rec,
			     struct ib_ah_attr *ah_attr);

/**
 * ib_init_ah_from_path - Initialize address handle attributes based on an SA
 *   path record.
 */
int ib_init_ah_from_path(struct ib_device *device, u8 port_num,
			 struct ib_sa_path_rec *rec,
			 struct ib_ah_attr *ah_attr);

/**
 * ib_sa_pack_path - Conert a path record from struct ib_sa_path_rec
 * to IB MAD wire format.
 */
void ib_sa_pack_path(struct ib_sa_path_rec *rec, void *attribute);

/**
 * ib_sa_unpack_path - Convert a path record from MAD format to struct
 * ib_sa_path_rec.
 */
void ib_sa_unpack_path(void *attribute, struct ib_sa_path_rec *rec);

/* Support GuidInfoRecord */
int ib_sa_guid_info_rec_query(struct ib_sa_client *client,
			      struct ib_device *device, u8 port_num,
			      struct ib_sa_guidinfo_rec *rec,
			      ib_sa_comp_mask comp_mask, u8 method,
			      int timeout_ms, gfp_t gfp_mask,
			      void (*callback)(int status,
					       struct ib_sa_guidinfo_rec *resp,
					       void *context),
			      void *context,
			      struct ib_sa_query **sa_query);

#endif /* IB_SA_H */
