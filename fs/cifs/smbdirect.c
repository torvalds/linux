/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *
 *   Author(s): Long Li <longli@microsoft.com>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 */
#include "smbdirect.h"

/* SMBD version number */
#define SMBD_V1	0x0100

/* Port numbers for SMBD transport */
#define SMB_PORT	445
#define SMBD_PORT	5445

/* Address lookup and resolve timeout in ms */
#define RDMA_RESOLVE_TIMEOUT	5000

/* SMBD negotiation timeout in seconds */
#define SMBD_NEGOTIATE_TIMEOUT	120

/* SMBD minimum receive size and fragmented sized defined in [MS-SMBD] */
#define SMBD_MIN_RECEIVE_SIZE		128
#define SMBD_MIN_FRAGMENTED_SIZE	131072

/*
 * Default maximum number of RDMA read/write outstanding on this connection
 * This value is possibly decreased during QP creation on hardware limit
 */
#define SMBD_CM_RESPONDER_RESOURCES	32

/* Maximum number of retries on data transfer operations */
#define SMBD_CM_RETRY			6
/* No need to retry on Receiver Not Ready since SMBD manages credits */
#define SMBD_CM_RNR_RETRY		0

/*
 * User configurable initial values per SMBD transport connection
 * as defined in [MS-SMBD] 3.1.1.1
 * Those may change after a SMBD negotiation
 */
/* The local peer's maximum number of credits to grant to the peer */
int smbd_receive_credit_max = 255;

/* The remote peer's credit request of local peer */
int smbd_send_credit_target = 255;

/* The maximum single message size can be sent to remote peer */
int smbd_max_send_size = 1364;

/*  The maximum fragmented upper-layer payload receive size supported */
int smbd_max_fragmented_recv_size = 1024 * 1024;

/*  The maximum single-message size which can be received */
int smbd_max_receive_size = 8192;

/* The timeout to initiate send of a keepalive message on idle */
int smbd_keep_alive_interval = 120;

/*
 * User configurable initial values for RDMA transport
 * The actual values used may be lower and are limited to hardware capabilities
 */
/* Default maximum number of SGEs in a RDMA write/read */
int smbd_max_frmr_depth = 2048;

/* If payload is less than this byte, use RDMA send/recv not read/write */
int rdma_readwrite_threshold = 4096;
