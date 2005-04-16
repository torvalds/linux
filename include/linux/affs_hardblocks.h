#ifndef	AFFS_HARDBLOCKS_H
#define	AFFS_HARDBLOCKS_H

/* Just the needed definitions for the RDB of an Amiga HD. */

struct RigidDiskBlock {
	u32	rdb_ID;
	__be32	rdb_SummedLongs;
	s32	rdb_ChkSum;
	u32	rdb_HostID;
	__be32	rdb_BlockBytes;
	u32	rdb_Flags;
	u32	rdb_BadBlockList;
	__be32	rdb_PartitionList;
	u32	rdb_FileSysHeaderList;
	u32	rdb_DriveInit;
	u32	rdb_Reserved1[6];
	u32	rdb_Cylinders;
	u32	rdb_Sectors;
	u32	rdb_Heads;
	u32	rdb_Interleave;
	u32	rdb_Park;
	u32	rdb_Reserved2[3];
	u32	rdb_WritePreComp;
	u32	rdb_ReducedWrite;
	u32	rdb_StepRate;
	u32	rdb_Reserved3[5];
	u32	rdb_RDBBlocksLo;
	u32	rdb_RDBBlocksHi;
	u32	rdb_LoCylinder;
	u32	rdb_HiCylinder;
	u32	rdb_CylBlocks;
	u32	rdb_AutoParkSeconds;
	u32	rdb_HighRDSKBlock;
	u32	rdb_Reserved4;
	char	rdb_DiskVendor[8];
	char	rdb_DiskProduct[16];
	char	rdb_DiskRevision[4];
	char	rdb_ControllerVendor[8];
	char	rdb_ControllerProduct[16];
	char	rdb_ControllerRevision[4];
	u32	rdb_Reserved5[10];
};

#define	IDNAME_RIGIDDISK	0x5244534B	/* "RDSK" */

struct PartitionBlock {
	__be32	pb_ID;
	__be32	pb_SummedLongs;
	s32	pb_ChkSum;
	u32	pb_HostID;
	__be32	pb_Next;
	u32	pb_Flags;
	u32	pb_Reserved1[2];
	u32	pb_DevFlags;
	u8	pb_DriveName[32];
	u32	pb_Reserved2[15];
	__be32	pb_Environment[17];
	u32	pb_EReserved[15];
};

#define	IDNAME_PARTITION	0x50415254	/* "PART" */

#define RDB_ALLOCATION_LIMIT	16

#endif	/* AFFS_HARDBLOCKS_H */
