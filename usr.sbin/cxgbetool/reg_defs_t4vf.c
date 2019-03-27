/*
 * This file is _NOT_ automatically generated.  It must agree with the
 * Virtual Function register map definitions in t4vf_defs.h in the common
 * code.
 */
__FBSDID("$FreeBSD$");

struct reg_info t4vf_sge_regs[] = {
	{ "SGE_KDOORBELL",			0x000, 0 },
		{ "QID", 15, 17 },
		{ "Priority", 14, 1 },
		{ "PIDX", 0, 14 },
	{ "SGE_GTS",				0x004, 0 },
		{ "IngressQID", 16, 16 },
		{ "TimerReg", 13, 3 },
		{ "SEIntArm", 12, 1 },
		{ "CIDXInc", 0, 12 },

	{ NULL, 0, 0 }
};

struct reg_info t5vf_sge_regs[] = {
	{ "SGE_VF_KDOORBELL",			0x000, 0 },
		{ "QID", 15, 17 },
		{ "Priority", 14, 1 },
		{ "Type", 13, 1 },
		{ "PIDX", 0, 13 },
	{ "SGE_VF_GTS",				0x004, 0 },
		{ "IngressQID", 16, 16 },
		{ "TimerReg", 13, 3 },
		{ "SEIntArm", 12, 1 },
		{ "CIDXInc", 0, 12 },

	{ NULL, 0, 0 }
};

struct reg_info t4vf_mps_regs[] = {
	{ "MPS_VF_CTL",	0x100, 0 },
		{ "TxEn", 1, 1 },
		{ "RxEn", 0, 1 },

	{ "MPS_VF_STAT_TX_VF_BCAST_BYTES_L",	0x180, 0 },
	{ "MPS_VF_STAT_TX_VF_BCAST_BYTES_H",	0x184, 0 },
	{ "MPS_VF_STAT_TX_VF_BCAST_FRAMES_L",	0x188, 0 },
	{ "MPS_VF_STAT_TX_VF_BCAST_FRAMES_H",	0x18c, 0 },

	{ "MPS_VF_STAT_TX_VF_MCAST_BYTES_L",	0x190, 0 },
	{ "MPS_VF_STAT_TX_VF_MCAST_BYTES_H",	0x194, 0 },
	{ "MPS_VF_STAT_TX_VF_MCAST_FRAMES_L",	0x198, 0 },
	{ "MPS_VF_STAT_TX_VF_MCAST_FRAMES_H",	0x19c, 0 },

	{ "MPS_VF_STAT_TX_VF_UCAST_BYTES_L",	0x1a0, 0 },
	{ "MPS_VF_STAT_TX_VF_UCAST_BYTES_H",	0x1a4, 0 },
	{ "MPS_VF_STAT_TX_VF_UCAST_FRAMES_L",	0x1a8, 0 },
	{ "MPS_VF_STAT_TX_VF_UCAST_FRAMES_H",	0x1ac, 0 },

	{ "MPS_VF_STAT_TX_VF_DROP_FRAMES_L",	0x1b0, 0 },
	{ "MPS_VF_STAT_TX_VF_DROP_FRAMES_H",	0x1b4, 0 },

	{ "MPS_VF_STAT_TX_VF_OFFLOAD_BYTES_L",  0x1b8, 0 },
	{ "MPS_VF_STAT_TX_VF_OFFLOAD_BYTES_H",  0x1bc, 0 },
	{ "MPS_VF_STAT_TX_VF_OFFLOAD_FRAMES_L",	0x1c0, 0 },
	{ "MPS_VF_STAT_TX_VF_OFFLOAD_FRAMES_H",	0x1c4, 0 },

	{ "MPS_VF_STAT_RX_VF_BCAST_BYTES_L",	0x1c8, 0 },
	{ "MPS_VF_STAT_RX_VF_BCAST_BYTES_H",	0x1cc, 0 },
	{ "MPS_VF_STAT_RX_VF_BCAST_FRAMES_L",	0x1d0, 0 },
	{ "MPS_VF_STAT_RX_VF_BCAST_FRAMES_H",	0x1d4, 0 },

	{ "MPS_VF_STAT_RX_VF_MCAST_BYTES_L",	0x1d8, 0 },
	{ "MPS_VF_STAT_RX_VF_MCAST_BYTES_H",	0x1dc, 0 },
	{ "MPS_VF_STAT_RX_VF_MCAST_FRAMES_L",	0x1e0, 0 },
	{ "MPS_VF_STAT_RX_VF_MCAST_FRAMES_H",	0x1e4, 0 },

	{ "MPS_VF_STAT_RX_VF_UCAST_BYTES_L",	0x1e8, 0 },
	{ "MPS_VF_STAT_RX_VF_UCAST_BYTES_H",	0x1ec, 0 },
	{ "MPS_VF_STAT_RX_VF_UCAST_FRAMES_L",	0x1f0, 0 },
	{ "MPS_VF_STAT_RX_VF_UCAST_FRAMES_H",	0x1f4, 0 },

	{ "MPS_VF_STAT_RX_VF_ERR_FRAMES_L",	0x1f8, 0 },
	{ "MPS_VF_STAT_RX_VF_ERR_FRAMES_H",	0x1fc, 0 },

	{ NULL, 0, 0 }
};

struct reg_info t4vf_pl_regs[] = {
	{ "PL_VF_WHOAMI",			0x200, 0 },
		{ "PortxMap", 24, 3 },
		{ "SourceBus", 16, 2 },
		{ "SourcePF", 8, 3 },
		{ "IsVF", 7, 1 },
		{ "VFID", 0, 7 },

	{ NULL, 0, 0 }
};

struct reg_info t5vf_pl_regs[] = {
	{ "PL_WHOAMI",				0x200, 0 },
		{ "PortxMap", 24, 3 },
		{ "SourceBus", 16, 2 },
		{ "SourcePF", 8, 3 },
		{ "IsVF", 7, 1 },
		{ "VFID", 0, 7 },
	{ "PL_VF_REV",				0x204, 0 },
		{ "ChipID", 4, 4 },
		{ "Rev", 0, 4 },
	{ "PL_VF_REVISION",			0x208, 0 },

	{ NULL, 0, 0 }
};

struct reg_info t6vf_pl_regs[] = {
	{ "PL_WHOAMI",				0x200, 0 },
		{ "PortxMap", 24, 3 },
		{ "SourceBus", 16, 2 },
		{ "SourcePF", 9, 3 },
		{ "IsVF", 8, 1 },
		{ "VFID", 0, 8 },
	{ "PL_VF_REV",				0x204, 0 },
		{ "ChipID", 4, 4 },
		{ "Rev", 0, 4 },
	{ "PL_VF_REVISION",			0x208, 0 },

	{ NULL, 0, 0 }
};

struct reg_info t4vf_cim_regs[] = {
	/*
	 * Note: the Mailbox Control register has read side-effects so
	 * the driver simply returns 0xffff for this register.
	 */
	{ "CIM_VF_EXT_MAILBOX_CTRL",		0x300, 0 },
		{ "MBGeneric", 4, 4 },
		{ "MBMsgValid", 3, 1 },
		{ "MBIntReq", 2, 1 },
		{ "MBOwner", 0, 2 },
	{ "CIM_VF_EXT_MAILBOX_STATUS",		0x304, 0 },
		{ "MBVFReady", 0, 1 },

	{ NULL, 0, 0 }
};

struct reg_info t4vf_mbdata_regs[] = {
	{ "CIM_VF_EXT_MAILBOX_DATA_00",		0x240, 0 },
		{ "Return", 8, 8 },
		{ "Length16", 0, 8 },
	{ "CIM_VF_EXT_MAILBOX_DATA_04",		0x244, 0 },
		{ "OpCode", 24, 8 },
		{ "Request", 23, 1 },
		{ "Read", 22, 1 },
		{ "Write", 21, 1 },
		{ "Execute", 20, 1 },
	{ "CIM_VF_EXT_MAILBOX_DATA_08",		0x248, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_0c",		0x24c, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_10",		0x250, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_14",		0x254, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_18",		0x258, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_1c",		0x25c, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_20",		0x260, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_24",		0x264, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_28",		0x268, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_2c",		0x26c, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_30",		0x270, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_34",		0x274, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_38",		0x278, 0 },
	{ "CIM_VF_EXT_MAILBOX_DATA_3c",		0x27c, 0 },

	{ NULL, 0, 0 }
};
