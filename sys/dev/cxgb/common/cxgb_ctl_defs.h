/*
 * Copyright (C) 2003-2006 Chelsio Communications.  All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 * release for licensing terms and conditions.
 *
 * $FreeBSD$
 */

#ifndef _CXGB3_OFFLOAD_CTL_DEFS_H
#define _CXGB3_OFFLOAD_CTL_DEFS_H

enum {
	GET_MAX_OUTSTANDING_WR,
	GET_TX_MAX_CHUNK,
	GET_TID_RANGE,
	GET_STID_RANGE,
	GET_RTBL_RANGE,
	GET_L2T_CAPACITY,
	GET_MTUS,
	GET_WR_LEN,
	GET_IFF_FROM_MAC,
	GET_DDP_PARAMS,
	GET_PORTS,

	ULP_ISCSI_GET_PARAMS,
	ULP_ISCSI_SET_PARAMS,

	RDMA_GET_PARAMS,
	RDMA_CQ_OP,
	RDMA_CQ_SETUP,
	RDMA_CQ_DISABLE,
	RDMA_CTRL_QP_SETUP,
	RDMA_GET_MEM,

	FAILOVER           = 30,
	FAILOVER_DONE      = 31,
	FAILOVER_CLEAR     = 32,

	GET_CPUIDX_OF_QSET = 40,

	GET_RX_PAGE_INFO   = 50,
};

/*
 * Structure used to describe a TID range.  Valid TIDs are [base, base+num).
 */
struct tid_range {
	unsigned int base;   /* first TID */
	unsigned int num;    /* number of TIDs in range */
};

/*
 * Structure used to request the size and contents of the MTU table.
 */
struct mtutab {
	unsigned int size;          /* # of entries in the MTU table */
	const unsigned short *mtus; /* the MTU table values */
};

/*
 * Structure used to request the ifnet that owns a given MAC address.
 */
struct iff_mac {
	struct ifnet *dev;
	const unsigned char *mac_addr;
	u16 vlan_tag;
};

struct pci_dev;

/*
 * Structure used to request the TCP DDP parameters.
 */
struct ddp_params {
	unsigned int llimit;     /* TDDP region start address */
	unsigned int ulimit;     /* TDDP region end address */
	unsigned int tag_mask;   /* TDDP tag mask */
	struct pci_dev *pdev;
};

struct adap_ports {
	unsigned int nports;     /* number of ports on this adapter */
	struct ifnet *lldevs[MAX_NPORTS];
};

/*
 * Structure used to return information to the iscsi layer.
 */
struct ulp_iscsi_info {
	unsigned int	offset;
	unsigned int	llimit;
	unsigned int	ulimit;
	unsigned int	tagmask;
	unsigned int	pgsz3;
	unsigned int	pgsz2;
	unsigned int	pgsz1;
	unsigned int	pgsz0;
	unsigned int	max_rxsz;
	unsigned int	max_txsz;
	struct pci_dev	*pdev;
};

/*
 * Offload TX/RX page information.
 */
struct ofld_page_info {
	unsigned int page_size;  /* Page size, should be a power of 2 */
	unsigned int num;        /* Number of pages */
};

/*
 * Structure used to return information to the RDMA layer.
 */
struct rdma_info {
	unsigned int tpt_base;   /* TPT base address */
	unsigned int tpt_top;	 /* TPT last entry address */
	unsigned int pbl_base;   /* PBL base address */
	unsigned int pbl_top;	 /* PBL last entry address */
	unsigned int rqt_base;   /* RQT base address */
	unsigned int rqt_top;	 /* RQT last entry address */
	unsigned int udbell_len; /* user doorbell region length */
	unsigned long udbell_physbase;  /* user doorbell physical start addr */
	void *kdb_addr;  /* kernel doorbell register address */
	struct device *pdev;    /* associated PCI device */
};

/*
 * Structure used to request an operation on an RDMA completion queue.
 */
struct rdma_cq_op {
	unsigned int id;
	unsigned int op;
	unsigned int credits;
};

/*
 * Structure used to setup RDMA completion queues.
 */
struct rdma_cq_setup {
	unsigned int id;
	unsigned long long base_addr;
	unsigned int size;
	unsigned int credits;
	unsigned int credit_thres;
	unsigned int ovfl_mode;
};

/*
 * Structure used to setup the RDMA control egress context.
 */
struct rdma_ctrlqp_setup {
	unsigned long long base_addr;
	unsigned int size;
};
#endif /* _CXGB3_OFFLOAD_CTL_DEFS_H */
