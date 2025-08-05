/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (c) 2025 Stefan Metzmacher
 */

#ifndef __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__
#define __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__

enum smbdirect_socket_status {
	SMBDIRECT_SOCKET_CREATED,
	SMBDIRECT_SOCKET_CONNECTING,
	SMBDIRECT_SOCKET_CONNECTED,
	SMBDIRECT_SOCKET_NEGOTIATE_FAILED,
	SMBDIRECT_SOCKET_DISCONNECTING,
	SMBDIRECT_SOCKET_DISCONNECTED,
	SMBDIRECT_SOCKET_DESTROYED
};

struct smbdirect_socket {
	enum smbdirect_socket_status status;

	/* RDMA related */
	struct {
		struct rdma_cm_id *cm_id;
	} rdma;

	/* IB verbs related */
	struct {
		struct ib_pd *pd;
		struct ib_cq *send_cq;
		struct ib_cq *recv_cq;

		/*
		 * shortcuts for rdma.cm_id->{qp,device};
		 */
		struct ib_qp *qp;
		struct ib_device *dev;
	} ib;

	struct smbdirect_socket_parameters parameters;

	/*
	 * The state for posted receive buffers
	 */
	struct {
		/*
		 * The type of PDU we are expecting
		 */
		enum {
			SMBDIRECT_EXPECT_NEGOTIATE_REQ = 1,
			SMBDIRECT_EXPECT_NEGOTIATE_REP = 2,
			SMBDIRECT_EXPECT_DATA_TRANSFER = 3,
		} expected;
	} recv_io;
};

struct smbdirect_recv_io {
	struct smbdirect_socket *socket;
	struct ib_cqe cqe;
	struct ib_sge sge;

	/* Link to free or reassembly list */
	struct list_head list;

	/* Indicate if this is the 1st packet of a payload */
	bool first_segment;

	/* SMBD packet header and payload follows this structure */
	u8 packet[];
};

#endif /* __FS_SMB_COMMON_SMBDIRECT_SMBDIRECT_SOCKET_H__ */
