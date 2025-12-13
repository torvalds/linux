/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 */

#ifndef __KSMBD_TRANSPORT_RDMA_H__
#define __KSMBD_TRANSPORT_RDMA_H__

#define SMBD_DEFAULT_IOSIZE (8 * 1024 * 1024)
#define SMBD_MIN_IOSIZE (512 * 1024)
#define SMBD_MAX_IOSIZE (16 * 1024 * 1024)

#ifdef CONFIG_SMB_SERVER_SMBDIRECT
int ksmbd_rdma_init(void);
void ksmbd_rdma_stop_listening(void);
void ksmbd_rdma_destroy(void);
bool ksmbd_rdma_capable_netdev(struct net_device *netdev);
void init_smbd_max_io_size(unsigned int sz);
unsigned int get_smbd_max_read_write_size(struct ksmbd_transport *kt);
#else
static inline int ksmbd_rdma_init(void) { return 0; }
static inline void ksmbd_rdma_stop_listening(void) { }
static inline void ksmbd_rdma_destroy(void) { }
static inline bool ksmbd_rdma_capable_netdev(struct net_device *netdev) { return false; }
static inline void init_smbd_max_io_size(unsigned int sz) { }
static inline unsigned int get_smbd_max_read_write_size(struct ksmbd_transport *kt) { return 0; }
#endif

#endif /* __KSMBD_TRANSPORT_RDMA_H__ */
