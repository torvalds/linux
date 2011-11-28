/*
 * RapidIO register definitions
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef LINUX_RIO_REGS_H
#define LINUX_RIO_REGS_H

/*
 * In RapidIO, each device has a 16MB configuration space that is
 * accessed via maintenance transactions.  Portions of configuration
 * space are standardized and/or reserved.
 */
#define RIO_MAINT_SPACE_SZ	0x1000000 /* 16MB of RapidIO mainenance space */

#define RIO_DEV_ID_CAR		0x00	/* [I] Device Identity CAR */
#define RIO_DEV_INFO_CAR	0x04	/* [I] Device Information CAR */
#define RIO_ASM_ID_CAR		0x08	/* [I] Assembly Identity CAR */
#define  RIO_ASM_ID_MASK		0xffff0000	/* [I] Asm ID Mask */
#define  RIO_ASM_VEN_ID_MASK		0x0000ffff	/* [I] Asm Vend Mask */

#define RIO_ASM_INFO_CAR	0x0c	/* [I] Assembly Information CAR */
#define  RIO_ASM_REV_MASK		0xffff0000	/* [I] Asm Rev Mask */
#define  RIO_EXT_FTR_PTR_MASK		0x0000ffff	/* [I] EF_PTR Mask */

#define RIO_PEF_CAR		0x10	/* [I] Processing Element Features CAR */
#define  RIO_PEF_BRIDGE			0x80000000	/* [I] Bridge */
#define  RIO_PEF_MEMORY			0x40000000	/* [I] MMIO */
#define  RIO_PEF_PROCESSOR		0x20000000	/* [I] Processor */
#define  RIO_PEF_SWITCH			0x10000000	/* [I] Switch */
#define  RIO_PEF_MULTIPORT		0x08000000	/* [VI, 2.1] Multiport */
#define  RIO_PEF_INB_MBOX		0x00f00000	/* [II, <= 1.2] Mailboxes */
#define  RIO_PEF_INB_MBOX0		0x00800000	/* [II, <= 1.2] Mailbox 0 */
#define  RIO_PEF_INB_MBOX1		0x00400000	/* [II, <= 1.2] Mailbox 1 */
#define  RIO_PEF_INB_MBOX2		0x00200000	/* [II, <= 1.2] Mailbox 2 */
#define  RIO_PEF_INB_MBOX3		0x00100000	/* [II, <= 1.2] Mailbox 3 */
#define  RIO_PEF_INB_DOORBELL		0x00080000	/* [II, <= 1.2] Doorbells */
#define  RIO_PEF_EXT_RT			0x00000200	/* [III, 1.3] Extended route table support */
#define  RIO_PEF_STD_RT			0x00000100	/* [III, 1.3] Standard route table support */
#define  RIO_PEF_CTLS			0x00000010	/* [III] CTLS */
#define  RIO_PEF_EXT_FEATURES		0x00000008	/* [I] EFT_PTR valid */
#define  RIO_PEF_ADDR_66		0x00000004	/* [I] 66 bits */
#define  RIO_PEF_ADDR_50		0x00000002	/* [I] 50 bits */
#define  RIO_PEF_ADDR_34		0x00000001	/* [I] 34 bits */

#define RIO_SWP_INFO_CAR	0x14	/* [I] Switch Port Information CAR */
#define  RIO_SWP_INFO_PORT_TOTAL_MASK	0x0000ff00	/* [I] Total number of ports */
#define  RIO_SWP_INFO_PORT_NUM_MASK	0x000000ff	/* [I] Maintenance transaction port number */
#define  RIO_GET_TOTAL_PORTS(x)		((x & RIO_SWP_INFO_PORT_TOTAL_MASK) >> 8)
#define  RIO_GET_PORT_NUM(x)		(x & RIO_SWP_INFO_PORT_NUM_MASK)

#define RIO_SRC_OPS_CAR		0x18	/* [I] Source Operations CAR */
#define  RIO_SRC_OPS_READ		0x00008000	/* [I] Read op */
#define  RIO_SRC_OPS_WRITE		0x00004000	/* [I] Write op */
#define  RIO_SRC_OPS_STREAM_WRITE	0x00002000	/* [I] Str-write op */
#define  RIO_SRC_OPS_WRITE_RESPONSE	0x00001000	/* [I] Write/resp op */
#define  RIO_SRC_OPS_DATA_MSG		0x00000800	/* [II] Data msg op */
#define  RIO_SRC_OPS_DOORBELL		0x00000400	/* [II] Doorbell op */
#define  RIO_SRC_OPS_ATOMIC_TST_SWP	0x00000100	/* [I] Atomic TAS op */
#define  RIO_SRC_OPS_ATOMIC_INC		0x00000080	/* [I] Atomic inc op */
#define  RIO_SRC_OPS_ATOMIC_DEC		0x00000040	/* [I] Atomic dec op */
#define  RIO_SRC_OPS_ATOMIC_SET		0x00000020	/* [I] Atomic set op */
#define  RIO_SRC_OPS_ATOMIC_CLR		0x00000010	/* [I] Atomic clr op */
#define  RIO_SRC_OPS_PORT_WRITE		0x00000004	/* [I] Port-write op */

#define RIO_DST_OPS_CAR		0x1c	/* Destination Operations CAR */
#define  RIO_DST_OPS_READ		0x00008000	/* [I] Read op */
#define  RIO_DST_OPS_WRITE		0x00004000	/* [I] Write op */
#define  RIO_DST_OPS_STREAM_WRITE	0x00002000	/* [I] Str-write op */
#define  RIO_DST_OPS_WRITE_RESPONSE	0x00001000	/* [I] Write/resp op */
#define  RIO_DST_OPS_DATA_MSG		0x00000800	/* [II] Data msg op */
#define  RIO_DST_OPS_DOORBELL		0x00000400	/* [II] Doorbell op */
#define  RIO_DST_OPS_ATOMIC_TST_SWP	0x00000100	/* [I] Atomic TAS op */
#define  RIO_DST_OPS_ATOMIC_INC		0x00000080	/* [I] Atomic inc op */
#define  RIO_DST_OPS_ATOMIC_DEC		0x00000040	/* [I] Atomic dec op */
#define  RIO_DST_OPS_ATOMIC_SET		0x00000020	/* [I] Atomic set op */
#define  RIO_DST_OPS_ATOMIC_CLR		0x00000010	/* [I] Atomic clr op */
#define  RIO_DST_OPS_PORT_WRITE		0x00000004	/* [I] Port-write op */

#define  RIO_OPS_READ			0x00008000	/* [I] Read op */
#define  RIO_OPS_WRITE			0x00004000	/* [I] Write op */
#define  RIO_OPS_STREAM_WRITE		0x00002000	/* [I] Str-write op */
#define  RIO_OPS_WRITE_RESPONSE		0x00001000	/* [I] Write/resp op */
#define  RIO_OPS_DATA_MSG		0x00000800	/* [II] Data msg op */
#define  RIO_OPS_DOORBELL		0x00000400	/* [II] Doorbell op */
#define  RIO_OPS_ATOMIC_TST_SWP		0x00000100	/* [I] Atomic TAS op */
#define  RIO_OPS_ATOMIC_INC		0x00000080	/* [I] Atomic inc op */
#define  RIO_OPS_ATOMIC_DEC		0x00000040	/* [I] Atomic dec op */
#define  RIO_OPS_ATOMIC_SET		0x00000020	/* [I] Atomic set op */
#define  RIO_OPS_ATOMIC_CLR		0x00000010	/* [I] Atomic clr op */
#define  RIO_OPS_PORT_WRITE		0x00000004	/* [I] Port-write op */

					/* 0x20-0x30 *//* Reserved */

#define	RIO_SWITCH_RT_LIMIT	0x34	/* [III, 1.3] Switch Route Table Destination ID Limit CAR */
#define	 RIO_RT_MAX_DESTID		0x0000ffff

#define RIO_MBOX_CSR		0x40	/* [II, <= 1.2] Mailbox CSR */
#define  RIO_MBOX0_AVAIL		0x80000000	/* [II] Mbox 0 avail */
#define  RIO_MBOX0_FULL			0x40000000	/* [II] Mbox 0 full */
#define  RIO_MBOX0_EMPTY		0x20000000	/* [II] Mbox 0 empty */
#define  RIO_MBOX0_BUSY			0x10000000	/* [II] Mbox 0 busy */
#define  RIO_MBOX0_FAIL			0x08000000	/* [II] Mbox 0 fail */
#define  RIO_MBOX0_ERROR		0x04000000	/* [II] Mbox 0 error */
#define  RIO_MBOX1_AVAIL		0x00800000	/* [II] Mbox 1 avail */
#define  RIO_MBOX1_FULL			0x00200000	/* [II] Mbox 1 full */
#define  RIO_MBOX1_EMPTY		0x00200000	/* [II] Mbox 1 empty */
#define  RIO_MBOX1_BUSY			0x00100000	/* [II] Mbox 1 busy */
#define  RIO_MBOX1_FAIL			0x00080000	/* [II] Mbox 1 fail */
#define  RIO_MBOX1_ERROR		0x00040000	/* [II] Mbox 1 error */
#define  RIO_MBOX2_AVAIL		0x00008000	/* [II] Mbox 2 avail */
#define  RIO_MBOX2_FULL			0x00004000	/* [II] Mbox 2 full */
#define  RIO_MBOX2_EMPTY		0x00002000	/* [II] Mbox 2 empty */
#define  RIO_MBOX2_BUSY			0x00001000	/* [II] Mbox 2 busy */
#define  RIO_MBOX2_FAIL			0x00000800	/* [II] Mbox 2 fail */
#define  RIO_MBOX2_ERROR		0x00000400	/* [II] Mbox 2 error */
#define  RIO_MBOX3_AVAIL		0x00000080	/* [II] Mbox 3 avail */
#define  RIO_MBOX3_FULL			0x00000040	/* [II] Mbox 3 full */
#define  RIO_MBOX3_EMPTY		0x00000020	/* [II] Mbox 3 empty */
#define  RIO_MBOX3_BUSY			0x00000010	/* [II] Mbox 3 busy */
#define  RIO_MBOX3_FAIL			0x00000008	/* [II] Mbox 3 fail */
#define  RIO_MBOX3_ERROR		0x00000004	/* [II] Mbox 3 error */

#define RIO_WRITE_PORT_CSR	0x44	/* [I, <= 1.2] Write Port CSR */
#define RIO_DOORBELL_CSR	0x44	/* [II, <= 1.2] Doorbell CSR */
#define  RIO_DOORBELL_AVAIL		0x80000000	/* [II] Doorbell avail */
#define  RIO_DOORBELL_FULL		0x40000000	/* [II] Doorbell full */
#define  RIO_DOORBELL_EMPTY		0x20000000	/* [II] Doorbell empty */
#define  RIO_DOORBELL_BUSY		0x10000000	/* [II] Doorbell busy */
#define  RIO_DOORBELL_FAILED		0x08000000	/* [II] Doorbell failed */
#define  RIO_DOORBELL_ERROR		0x04000000	/* [II] Doorbell error */
#define  RIO_WRITE_PORT_AVAILABLE	0x00000080	/* [I] Write Port Available */
#define  RIO_WRITE_PORT_FULL		0x00000040	/* [I] Write Port Full */
#define  RIO_WRITE_PORT_EMPTY		0x00000020	/* [I] Write Port Empty */
#define  RIO_WRITE_PORT_BUSY		0x00000010	/* [I] Write Port Busy */
#define  RIO_WRITE_PORT_FAILED		0x00000008	/* [I] Write Port Failed */
#define  RIO_WRITE_PORT_ERROR		0x00000004	/* [I] Write Port Error */

					/* 0x48 *//* Reserved */

#define RIO_PELL_CTRL_CSR	0x4c	/* [I] PE Logical Layer Control CSR */
#define   RIO_PELL_ADDR_66		0x00000004	/* [I] 66-bit addr */
#define   RIO_PELL_ADDR_50		0x00000002	/* [I] 50-bit addr */
#define   RIO_PELL_ADDR_34		0x00000001	/* [I] 34-bit addr */

					/* 0x50-0x54 *//* Reserved */

#define RIO_LCSH_BA		0x58	/* [I] LCS High Base Address */
#define RIO_LCSL_BA		0x5c	/* [I] LCS Base Address */

#define RIO_DID_CSR		0x60	/* [III] Base Device ID CSR */

					/* 0x64 *//* Reserved */

#define RIO_HOST_DID_LOCK_CSR	0x68	/* [III] Host Base Device ID Lock CSR */
#define RIO_COMPONENT_TAG_CSR	0x6c	/* [III] Component Tag CSR */

#define RIO_STD_RTE_CONF_DESTID_SEL_CSR	0x70
#define  RIO_STD_RTE_CONF_EXTCFGEN		0x80000000
#define RIO_STD_RTE_CONF_PORT_SEL_CSR	0x74
#define RIO_STD_RTE_DEFAULT_PORT	0x78

					/* 0x7c-0xf8 *//* Reserved */
					/* 0x100-0xfff8 *//* [I] Extended Features Space */
					/* 0x10000-0xfffff8 *//* [I] Implementation-defined Space */

/*
 * Extended Features Space is a configuration space area where
 * functionality is mapped into extended feature blocks via a
 * singly linked list of extended feature pointers (EFT_PTR).
 *
 * Each extended feature block can be identified/located in
 * Extended Features Space by walking the extended feature
 * list starting with the Extended Feature Pointer located
 * in the Assembly Information CAR.
 *
 * Extended Feature Blocks (EFBs) are identified with an assigned
 * EFB ID. Extended feature block offsets in the definitions are
 * relative to the offset of the EFB within the  Extended Features
 * Space.
 */

/* Helper macros to parse the Extended Feature Block header */
#define RIO_EFB_PTR_MASK	0xffff0000
#define RIO_EFB_ID_MASK		0x0000ffff
#define RIO_GET_BLOCK_PTR(x)	((x & RIO_EFB_PTR_MASK) >> 16)
#define RIO_GET_BLOCK_ID(x)	(x & RIO_EFB_ID_MASK)

/* Extended Feature Block IDs */
#define RIO_EFB_PAR_EP_ID	0x0001	/* [IV] LP/LVDS EP Devices */
#define RIO_EFB_PAR_EP_REC_ID	0x0002	/* [IV] LP/LVDS EP Recovery Devices */
#define RIO_EFB_PAR_EP_FREE_ID	0x0003	/* [IV] LP/LVDS EP Free Devices */
#define RIO_EFB_SER_EP_ID_V13P	0x0001	/* [VI] LP/Serial EP Devices, RapidIO Spec ver 1.3 and above */
#define RIO_EFB_SER_EP_REC_ID_V13P	0x0002	/* [VI] LP/Serial EP Recovery Devices, RapidIO Spec ver 1.3 and above */
#define RIO_EFB_SER_EP_FREE_ID_V13P	0x0003	/* [VI] LP/Serial EP Free Devices, RapidIO Spec ver 1.3 and above */
#define RIO_EFB_SER_EP_ID	0x0004	/* [VI] LP/Serial EP Devices */
#define RIO_EFB_SER_EP_REC_ID	0x0005	/* [VI] LP/Serial EP Recovery Devices */
#define RIO_EFB_SER_EP_FREE_ID	0x0006	/* [VI] LP/Serial EP Free Devices */
#define RIO_EFB_SER_EP_FREC_ID	0x0009  /* [VI] LP/Serial EP Free Recovery Devices */
#define RIO_EFB_ERR_MGMNT	0x0007  /* [VIII] Error Management Extensions */

/*
 * Physical 8/16 LP-LVDS
 * ID=0x0001, Generic End Point Devices
 * ID=0x0002, Generic End Point Devices, software assisted recovery option
 * ID=0x0003, Generic End Point Free Devices
 *
 * Physical LP-Serial
 * ID=0x0004, Generic End Point Devices
 * ID=0x0005, Generic End Point Devices, software assisted recovery option
 * ID=0x0006, Generic End Point Free Devices
 */
#define RIO_PORT_MNT_HEADER		0x0000
#define RIO_PORT_REQ_CTL_CSR		0x0020
#define RIO_PORT_RSP_CTL_CSR		0x0024	/* 0x0001/0x0002 */
#define RIO_PORT_LINKTO_CTL_CSR		0x0020	/* Serial */
#define RIO_PORT_RSPTO_CTL_CSR		0x0024	/* Serial */
#define RIO_PORT_GEN_CTL_CSR		0x003c
#define  RIO_PORT_GEN_HOST		0x80000000
#define  RIO_PORT_GEN_MASTER		0x40000000
#define  RIO_PORT_GEN_DISCOVERED	0x20000000
#define RIO_PORT_N_MNT_REQ_CSR(x)	(0x0040 + x*0x20)	/* 0x0002 */
#define  RIO_MNT_REQ_CMD_RD		0x03	/* Reset-device command */
#define  RIO_MNT_REQ_CMD_IS		0x04	/* Input-status command */
#define RIO_PORT_N_MNT_RSP_CSR(x)	(0x0044 + x*0x20)	/* 0x0002 */
#define  RIO_PORT_N_MNT_RSP_RVAL	0x80000000 /* Response Valid */
#define  RIO_PORT_N_MNT_RSP_ASTAT	0x000007e0 /* ackID Status */
#define  RIO_PORT_N_MNT_RSP_LSTAT	0x0000001f /* Link Status */
#define RIO_PORT_N_ACK_STS_CSR(x)	(0x0048 + x*0x20)	/* 0x0002 */
#define  RIO_PORT_N_ACK_CLEAR		0x80000000
#define  RIO_PORT_N_ACK_INBOUND		0x3f000000
#define  RIO_PORT_N_ACK_OUTSTAND	0x00003f00
#define  RIO_PORT_N_ACK_OUTBOUND	0x0000003f
#define RIO_PORT_N_ERR_STS_CSR(x)	(0x0058 + x*0x20)
#define  RIO_PORT_N_ERR_STS_PW_OUT_ES	0x00010000 /* Output Error-stopped */
#define  RIO_PORT_N_ERR_STS_PW_INP_ES	0x00000100 /* Input Error-stopped */
#define  RIO_PORT_N_ERR_STS_PW_PEND	0x00000010 /* Port-Write Pending */
#define  RIO_PORT_N_ERR_STS_PORT_ERR	0x00000004
#define  RIO_PORT_N_ERR_STS_PORT_OK	0x00000002
#define  RIO_PORT_N_ERR_STS_PORT_UNINIT	0x00000001
#define RIO_PORT_N_CTL_CSR(x)		(0x005c + x*0x20)
#define  RIO_PORT_N_CTL_PWIDTH		0xc0000000
#define  RIO_PORT_N_CTL_PWIDTH_1	0x00000000
#define  RIO_PORT_N_CTL_PWIDTH_4	0x40000000
#define  RIO_PORT_N_CTL_P_TYP_SER	0x00000001
#define  RIO_PORT_N_CTL_LOCKOUT		0x00000002
#define  RIO_PORT_N_CTL_EN_RX_SER	0x00200000
#define  RIO_PORT_N_CTL_EN_TX_SER	0x00400000
#define  RIO_PORT_N_CTL_EN_RX_PAR	0x08000000
#define  RIO_PORT_N_CTL_EN_TX_PAR	0x40000000

/*
 * Error Management Extensions (RapidIO 1.3+, Part 8)
 *
 * Extended Features Block ID=0x0007
 */

/* General EM Registers (Common for all Ports) */

#define RIO_EM_EFB_HEADER	0x000	/* Error Management Extensions Block Header */
#define RIO_EM_LTL_ERR_DETECT	0x008	/* Logical/Transport Layer Error Detect CSR */
#define RIO_EM_LTL_ERR_EN	0x00c	/* Logical/Transport Layer Error Enable CSR */
#define  REM_LTL_ERR_ILLTRAN		0x08000000 /* Illegal Transaction decode */
#define  REM_LTL_ERR_UNSOLR		0x00800000 /* Unsolicited Response */
#define  REM_LTL_ERR_UNSUPTR		0x00400000 /* Unsupported Transaction */
#define  REM_LTL_ERR_IMPSPEC		0x000000ff /* Implementation Specific */
#define RIO_EM_LTL_HIADDR_CAP	0x010	/* Logical/Transport Layer High Address Capture CSR */
#define RIO_EM_LTL_ADDR_CAP	0x014	/* Logical/Transport Layer Address Capture CSR */
#define RIO_EM_LTL_DEVID_CAP	0x018	/* Logical/Transport Layer Device ID Capture CSR */
#define RIO_EM_LTL_CTRL_CAP	0x01c	/* Logical/Transport Layer Control Capture CSR */
#define RIO_EM_PW_TGT_DEVID	0x028	/* Port-write Target deviceID CSR */
#define RIO_EM_PKT_TTL		0x02c	/* Packet Time-to-live CSR */

/* Per-Port EM Registers */

#define RIO_EM_PN_ERR_DETECT(x)	(0x040 + x*0x40) /* Port N Error Detect CSR */
#define  REM_PED_IMPL_SPEC		0x80000000
#define  REM_PED_LINK_TO		0x00000001
#define RIO_EM_PN_ERRRATE_EN(x) (0x044 + x*0x40) /* Port N Error Rate Enable CSR */
#define RIO_EM_PN_ATTRIB_CAP(x)	(0x048 + x*0x40) /* Port N Attributes Capture CSR */
#define RIO_EM_PN_PKT_CAP_0(x)	(0x04c + x*0x40) /* Port N Packet/Control Symbol Capture 0 CSR */
#define RIO_EM_PN_PKT_CAP_1(x)	(0x050 + x*0x40) /* Port N Packet Capture 1 CSR */
#define RIO_EM_PN_PKT_CAP_2(x)	(0x054 + x*0x40) /* Port N Packet Capture 2 CSR */
#define RIO_EM_PN_PKT_CAP_3(x)	(0x058 + x*0x40) /* Port N Packet Capture 3 CSR */
#define RIO_EM_PN_ERRRATE(x)	(0x068 + x*0x40) /* Port N Error Rate CSR */
#define RIO_EM_PN_ERRRATE_TR(x) (0x06c + x*0x40) /* Port N Error Rate Threshold CSR */

#endif				/* LINUX_RIO_REGS_H */
