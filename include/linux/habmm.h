/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef HABMM_H
#define HABMM_H

#include "linux/habmmid.h"

#define HAB_API_VER_DEF(_MAJOR_, _MINOR_) \
		((_MAJOR_&0xFF)<<16 | (_MINOR_&0xFFF))
#define HAB_API_VER	HAB_API_VER_DEF(1, 0)

#include <linux/types.h>

/* habmm_socket_open
 *
 * Description:
 *
 * Establish a communication channel between Virtual Machines. Blocks
 * until the connection is established between sender and receiver.
 * Client can call this APImultiple times with the same name to connect
 * to the same communication channel, the function returns a different context
 * for every open for proper resource allocation and client identification.
 *
 * Params:
 * out handle - An opaque handle associated with a successful virtual channel
 * creation in MM_ID - multimedia ID used to allocate the physical channels to
 * service all the virtual channels created through this open
 * in timeout - timeout value specified by the client to avoid forever block
 * in flags - future extension
 *
 * Return:
 * status (success/failure/timeout)
 *
 */

/* single FE-BE connection multi-to-multi point to point matching (default) */
#define HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_SINGLE_FE        0x00000000
/* one BE for one domU */
#define HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_SINGLE_DOMU      0x00000001
/* one BE for all the domUs */
#define HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_MULTI_DOMUS      0x00000002

/* This option is only available for HAB clients in kernel space, and it will
 * be HAB clients responsibility in kernel space to avoid calling any unexpected
 * uninterruptible habmm_socket_open() since it is not killable.
 */
#define HABMM_SOCKET_OPEN_FLAGS_UNINTERRUPTIBLE            0x00000004

int32_t habmm_socket_open(int32_t *handle, uint32_t mm_ip_id,
		uint32_t timeout, uint32_t flags);

/* habmm_socket_close
 *
 * Description:
 *
 * Tear down the virtual channel that was established through habmm_socket_open
 * and release all resources associated with it.
 *
 * Params:
 *
 * in handle - handle to the virtual channel created by habmm_socket_open
 *
 * Return:
 * status - (success/failure)
 *
 *
 */

int32_t habmm_socket_close(int32_t handle);


/* habmm_socket_send
 *
 * Description:
 *
 * Send data over the virtual channel
 *
 * Params:
 *
 * in handle - handle created by habmm_socket_open
 * in src_buff - data to be send across the virtual channel
 * inout size_bytes - size of the data to be send. Either the whole packet is
 *                    sent or not
 * in flags - future extension
 *
 * Return:
 * status (success/fail/disconnected)
 *
 */

/* Non-blocking mode: function will return immediately with HAB_AGAIN
 * if the send operation cannot be completed without blocking.
 */
#define HABMM_SOCKET_SEND_FLAGS_NON_BLOCKING 0x00000001

/* Collect cross-VM stats: client provides stat-buffer large enough to allow 2
 * sets of a 2-uint64_t pair to collect seconds and nano-seconds at the
 * beginning of the stat-buffer. Stats are collected when the stat-buffer leaves
 *  VM1, then enters VM2
 */
#define HABMM_SOCKET_SEND_FLAGS_XING_VM_STAT 0x00000002

/* start to measure cross-vm schedule latency: VM1 send msg with this flag
 * to VM2 to kick off the measurement. In the hab driver level, the VM1 hab
 * driver shall record the time of schedule out with mpm_timer, and buffer
 * it for later usage. The VM2 hab driver shall record the time of schedule
 * in with mpm_timer and pass it to "habtest" application.
 */
#define HABMM_SOCKET_XVM_SCHE_TEST 0x00000004

/* VM2 responds this message to VM1 for HABMM_SOCKET_XVM_SCHE_TEST.
 * In the hab driver level, the VM2 hab driver shall record the time of schedule
 * out with mpm_timer, and buffer it for later usage; the VM1 hab driver
 * shall record the time of schedule in with mpm_timer and pass it to "habtest"
 * application.
 */
#define HABMM_SOCKET_XVM_SCHE_TEST_ACK 0x00000008

/* VM1 sends this message to VM2 asking for collect all the mpm_timer values
 * to calculate the latency of schduling between VM1 and VM2. In the hab driver
 * level, the VM1 hab driver shall save the previous restored schduling out
 * time to the message buffer
 */
#define HABMM_SOCKET_XVM_SCHE_RESULT_REQ 0x00000010

/* VM2 responds this message to VM2 for HABMM_SOCKET_XVM_SCHE_RESULT_REQ.
 * In the habtest application level, VM2 shall save the previous restored
 * scheduling in time into message buffer, in the hab driver level, VM2
 * shall save the previous restored scheduling out time to the message
 * buffer.
 */
#define HABMM_SOCKET_XVM_SCHE_RESULT_RSP 0x00000020

struct habmm_xing_vm_stat {
	uint64_t tx_sec;
	uint64_t tx_usec;
	uint64_t rx_sec;
	uint64_t rx_usec;
};

int32_t habmm_socket_send(int32_t handle, void *src_buff, uint32_t size_bytes,
		uint32_t flags);


/* habmm_socket_recv
 *
 * Description:
 *
 * Receive data over the virtual channel created by habmm_socket_open.
 * Blocking until actual data is received or timeout value expires
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * inout dst_buff - buffer pointer to store received data
 * inout size_bytes - size of the dst_buff. returned value shows the actual
 *                    bytes received.
 * in timeout - timeout value specified by the client to avoid forever blocking,
 *				The unit of measurement is ms.
 *				0 is immediately timeout; -1 is forever blocking.
 * in flags - details as below.
 *
 *
 * Return:
 * status (success/failure/timeout/disconnected)
 *
 */

/* Non-blocking mode: function will return immediately if there is no data
 * available.
 */
#define HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING 0x00000001

/* In the blocking mode, this flag is used to indicate it is an
 * uninterruptbile blocking call.
 */
#define HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE 0x00000002

/* Enable timeout function, This flag is used to indicate that the timeout
 * function takes effect. Note that the timeout parameter is meaningful only if
 * this flag is added, otherwise the timeout parameter is ignored.
 * In addition, when the HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING flag is set,
 * the current flag is ignored.
 */
#define HABMM_SOCKET_RECV_FLAGS_TIMEOUT 0x00000004

int32_t habmm_socket_recv(int32_t handle, void *dst_buff, uint32_t *size_bytes,
		uint32_t timeout, uint32_t flags);

/* habmm_socket_sendto
 *
 * Description:
 *
 * This is for backend only. Send data over the virtual channel to remote
 * frontend virtual channel for multi-FEs-to-single-BE model when
 * the BE virtual channel is created using
 * HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_SINGLE_DOMU or
 * HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_MULTI_DOMUS
 *
 * Params:
 *
 * in handle - handle created by habmm_socket_open
 * in src_buff - data to be send across the virtual channel
 * inout size_bytes - size of the data to be send. The packet is fully sent on
 *                    success,or not sent at all upon any failure
 * in remote_handle - the destination of this send using remote FE's virtual
 *                    channel handle
 * in flags - future extension
 *
 * Return:
 * status (success/fail/disconnected)
 */
int32_t habmm_socket_sendto(int32_t handle, void *src_buff, uint32_t size_bytes,
		int32_t remote_handle, uint32_t flags);


/* habmm_socket_recvfrom
 *
 * Description:
 *
 * Receive data over the virtual channel created by habmm_socket_open.
 * Returned is the remote FE's virtual channel handle to be used for sendto.
 * Blocking until actual data is received or timeout value expires. This is for
 * BE running in multi-FEs-to-single-BE model when the BE virtual channel is
 * created using HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_SINGLE_DOMU or
 * HABMM_SOCKET_OPEN_FLAGS_SINGLE_BE_MULTI_DOMUS.
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * inout dst_buff - buffer pointer to store received data
 * inout size_bytes - size of the dst_buff. returned value shows the actual
 *                    bytes received.
 * in timeout - timeout value specified by the client to avoid forever block
 * in remote_handle - the FE who sent this message through the
 *                    connected virtual channel to BE.
 * in flags - future extension
 *
 * Return:
 * status (success/failure/timeout/disconnected)
 *
 */
int32_t habmm_socket_recvfrom(int32_t handle, void *dst_buff,
	uint32_t *size_bytes, uint32_t timeout,
	int32_t *remote_handle, uint32_t flags);

/* exporting memory type DMA : This is platform dependent for user mode. If it
 * does exist, HAB needs to use DMA method to retrieve the memory for exporting.
 * If it does not exist, this flag is ignored.
 */
#define HABMM_EXP_MEM_TYPE_DMA 0x00000001

/*
 * this flag is used for export from dma_buf fd or import to dma_buf fd
 */
#define HABMM_EXPIMP_FLAGS_FD     0x00010000
#define HABMM_EXPIMP_FLAGS_DMABUF 0x00020000

#define HAB_MAX_EXPORT_SIZE 0x8000000

/*
 * Description:
 *
 * Prepare the sharing of the buffer on the exporter side. The returned
 * reference ID needs to be sent to importer separately.
 * During sending the HAB will attach the actual exporting buffer information.
 * The exporting is per process space.
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * in buff_to_share - buffer to be exported
 * in size_bytes - size of the exporting buffer in bytes
 * out export_id - to be returned by this call upon success
 * in flags - future extension
 *
 * Return:
 * status (success/failure)
 *
 */
int32_t habmm_export(int32_t handle, void *buff_to_share, uint32_t size_bytes,
		uint32_t *export_id, uint32_t flags);

/*
 * Description:
 *
 * Free any allocated resource associated with this export IDin on local side.
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * in export_id - all resource allocated with export_id are to be freed
 * in flags - future extension
 *
 * Return:
 * status (success/failure)
 *
 */
int32_t habmm_unexport(int32_t handle, uint32_t export_id, uint32_t flags);

/*
 * Description:
 *
 * Import the exporter's shared reference ID.
 * The importing is per process space.
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * out buff_shared - buffer to be imported. returned upon success
 * in size_bytes - size of the imported buffer in bytes. It should match the
 *                 original exported buffer size
 * in export_id - received when exporter sent its exporting ID through
 *                habmm_socket_send() previously
 * in flags - future extension
 *
 * Return:
 * status (success/failure)
 *
 */

/* Non-blocking mode: function will return immediately if there is no data
 * available.  Supported only for kernel clients.
 */
#define HABMM_IMPORT_FLAGS_CACHED 0x00000001

int32_t habmm_import(int32_t handle, void **buff_shared, uint32_t size_bytes,
		uint32_t export_id, uint32_t flags);

/*
 * Description:
 *
 * Release any resource associated with the export ID on the importer side.
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * in export_id - received when exporter sent its exporting ID through
 *                habmm_socket_send() previously
 * in buff_shared - received from habmm_import() together with export_id
 * in flags - future extension
 *
 * Return:
 * status (success/failure)
 *
 */
int32_t habmm_unimport(int32_t handle, uint32_t export_id, void *buff_shared,
		uint32_t flags);

/*
 * Description:
 *
 * Query various information of the opened hab socket.
 *
 * Params:
 *
 * in handle - communication channel created by habmm_socket_open
 * in habmm_socket_info - retrieve socket information regarding local and remote
 *                        VMs
 * in flags - future extension
 *
 * Return:
 * status (success/failure)
 *
 */
#define VMNAME_SIZE 12

struct hab_socket_info {
	int32_t vmid_remote; /* habmm's vmid */
	int32_t vmid_local;
	/* name from hypervisor framework if available */
	char    vmname_remote[VMNAME_SIZE];
	char    vmname_local[VMNAME_SIZE];
};

int32_t habmm_socket_query(int32_t handle, struct hab_socket_info *info,
		uint32_t flags);

#endif /* HABMM_H */
