/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2007 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_GUTS_H__
#define __YAFFS_GUTS_H__

#include "devextras.h"
#include "yportenv.h"

#define YAFFS_OK	1
#define YAFFS_FAIL  0

/* Give us a  Y=0x59,
 * Give us an A=0x41,
 * Give us an FF=0xFF
 * Give us an S=0x53
 * And what have we got...
 */
#define YAFFS_MAGIC			0x5941FF53

#define YAFFS_NTNODES_LEVEL0	  	16
#define YAFFS_TNODES_LEVEL0_BITS	4
#define YAFFS_TNODES_LEVEL0_MASK	0xf

#define YAFFS_NTNODES_INTERNAL 		(YAFFS_NTNODES_LEVEL0 / 2)
#define YAFFS_TNODES_INTERNAL_BITS 	(YAFFS_TNODES_LEVEL0_BITS - 1)
#define YAFFS_TNODES_INTERNAL_MASK	0x7
#define YAFFS_TNODES_MAX_LEVEL		6

#ifndef CONFIG_YAFFS_NO_YAFFS1
#define YAFFS_BYTES_PER_SPARE		16
#define YAFFS_BYTES_PER_CHUNK		512
#define YAFFS_CHUNK_SIZE_SHIFT		9
#define YAFFS_CHUNKS_PER_BLOCK		32
#define YAFFS_BYTES_PER_BLOCK		(YAFFS_CHUNKS_PER_BLOCK*YAFFS_BYTES_PER_CHUNK)
#endif

#define YAFFS_MIN_YAFFS2_CHUNK_SIZE 	1024
#define YAFFS_MIN_YAFFS2_SPARE_SIZE	32

#define YAFFS_MAX_CHUNK_ID		0x000FFFFF

#define YAFFS_UNUSED_OBJECT_ID		0x0003FFFF

#define YAFFS_ALLOCATION_NOBJECTS	100
#define YAFFS_ALLOCATION_NTNODES	100
#define YAFFS_ALLOCATION_NLINKS		100

#define YAFFS_NOBJECT_BUCKETS		256


#define YAFFS_OBJECT_SPACE		0x40000

#define YAFFS_CHECKPOINT_VERSION 	3

#ifdef CONFIG_YAFFS_UNICODE
#define YAFFS_MAX_NAME_LENGTH		127
#define YAFFS_MAX_ALIAS_LENGTH		79
#else
#define YAFFS_MAX_NAME_LENGTH		255
#define YAFFS_MAX_ALIAS_LENGTH		159
#endif

#define YAFFS_SHORT_NAME_LENGTH		15

/* Some special object ids for pseudo objects */
#define YAFFS_OBJECTID_ROOT		1
#define YAFFS_OBJECTID_LOSTNFOUND	2
#define YAFFS_OBJECTID_UNLINKED		3
#define YAFFS_OBJECTID_DELETED		4

/* Sseudo object ids for checkpointing */
#define YAFFS_OBJECTID_SB_HEADER	0x10
#define YAFFS_OBJECTID_CHECKPOINT_DATA	0x20
#define YAFFS_SEQUENCE_CHECKPOINT_DATA  0x21

/* */

#define YAFFS_MAX_SHORT_OP_CACHES	20

#define YAFFS_N_TEMP_BUFFERS		6

/* We limit the number attempts at sucessfully saving a chunk of data.
 * Small-page devices have 32 pages per block; large-page devices have 64.
 * Default to something in the order of 5 to 10 blocks worth of chunks.
 */
#define YAFFS_WR_ATTEMPTS		(5*64)

/* Sequence numbers are used in YAFFS2 to determine block allocation order.
 * The range is limited slightly to help distinguish bad numbers from good.
 * This also allows us to perhaps in the future use special numbers for
 * special purposes.
 * EFFFFF00 allows the allocation of 8 blocks per second (~1Mbytes) for 15 years,
 * and is a larger number than the lifetime of a 2GB device.
 */
#define YAFFS_LOWEST_SEQUENCE_NUMBER	0x00001000
#define YAFFS_HIGHEST_SEQUENCE_NUMBER	0xEFFFFF00

/* Special sequence number for bad block that failed to be marked bad */
#define YAFFS_SEQUENCE_BAD_BLOCK	0xFFFF0000

/* ChunkCache is used for short read/write operations.*/
typedef struct {
	struct yaffs_ObjectStruct *object;
	int chunkId;
	int lastUse;
	int dirty;
	int nBytes;		/* Only valid if the cache is dirty */
	int locked;		/* Can't push out or flush while locked. */
#ifdef CONFIG_YAFFS_YAFFS2
	__u8 *data;
#else
	__u8 data[YAFFS_BYTES_PER_CHUNK];
#endif
} yaffs_ChunkCache;



/* Tags structures in RAM
 * NB This uses bitfield. Bitfields should not straddle a u32 boundary otherwise
 * the structure size will get blown out.
 */

#ifndef CONFIG_YAFFS_NO_YAFFS1
typedef struct {
	unsigned chunkId:20;
	unsigned serialNumber:2;
	unsigned byteCountLSB:10;
	unsigned objectId:18;
	unsigned ecc:12;
	unsigned byteCountMSB:2;
} yaffs_Tags;

typedef union {
	yaffs_Tags asTags;
	__u8 asBytes[8];
} yaffs_TagsUnion;

#endif

/* Stuff used for extended tags in YAFFS2 */

typedef enum {
	YAFFS_ECC_RESULT_UNKNOWN,
	YAFFS_ECC_RESULT_NO_ERROR,
	YAFFS_ECC_RESULT_FIXED,
	YAFFS_ECC_RESULT_UNFIXED
} yaffs_ECCResult;

typedef enum {
	YAFFS_OBJECT_TYPE_UNKNOWN,
	YAFFS_OBJECT_TYPE_FILE,
	YAFFS_OBJECT_TYPE_SYMLINK,
	YAFFS_OBJECT_TYPE_DIRECTORY,
	YAFFS_OBJECT_TYPE_HARDLINK,
	YAFFS_OBJECT_TYPE_SPECIAL
} yaffs_ObjectType;

#define YAFFS_OBJECT_TYPE_MAX YAFFS_OBJECT_TYPE_SPECIAL

typedef struct {

	unsigned validMarker0;
	unsigned chunkUsed;	/*  Status of the chunk: used or unused */
	unsigned objectId;	/* If 0 then this is not part of an object (unused) */
	unsigned chunkId;	/* If 0 then this is a header, else a data chunk */
	unsigned byteCount;	/* Only valid for data chunks */

	/* The following stuff only has meaning when we read */
	yaffs_ECCResult eccResult;
	unsigned blockBad;

	/* YAFFS 1 stuff */
	unsigned chunkDeleted;	/* The chunk is marked deleted */
	unsigned serialNumber;	/* Yaffs1 2-bit serial number */

	/* YAFFS2 stuff */
	unsigned sequenceNumber;	/* The sequence number of this block */

	/* Extra info if this is an object header (YAFFS2 only) */

	unsigned extraHeaderInfoAvailable;	/* There is extra info available if this is not zero */
	unsigned extraParentObjectId;	/* The parent object */
	unsigned extraIsShrinkHeader;	/* Is it a shrink header? */
	unsigned extraShadows;		/* Does this shadow another object? */

	yaffs_ObjectType extraObjectType;	/* What object type? */

	unsigned extraFileLength;		/* Length if it is a file */
	unsigned extraEquivalentObjectId;	/* Equivalent object Id if it is a hard link */

	unsigned validMarker1;

} yaffs_ExtendedTags;

/* Spare structure for YAFFS1 */
typedef struct {
	__u8 tagByte0;
	__u8 tagByte1;
	__u8 tagByte2;
	__u8 tagByte3;
	__u8 pageStatus;	/* set to 0 to delete the chunk */
	__u8 blockStatus;
	__u8 tagByte4;
	__u8 tagByte5;
	__u8 ecc1[3];
	__u8 tagByte6;
	__u8 tagByte7;
	__u8 ecc2[3];
} yaffs_Spare;

/*Special structure for passing through to mtd */
struct yaffs_NANDSpare {
	yaffs_Spare spare;
	int eccres1;
	int eccres2;
};

/* Block data in RAM */

typedef enum {
	YAFFS_BLOCK_STATE_UNKNOWN = 0,

	YAFFS_BLOCK_STATE_SCANNING,
	YAFFS_BLOCK_STATE_NEEDS_SCANNING,
	/* The block might have something on it (ie it is allocating or full, perhaps empty)
	 * but it needs to be scanned to determine its true state.
	 * This state is only valid during yaffs_Scan.
	 * NB We tolerate empty because the pre-scanner might be incapable of deciding
	 * However, if this state is returned on a YAFFS2 device, then we expect a sequence number
	 */

	YAFFS_BLOCK_STATE_EMPTY,
	/* This block is empty */

	YAFFS_BLOCK_STATE_ALLOCATING,
	/* This block is partially allocated.
	 * At least one page holds valid data.
	 * This is the one currently being used for page
	 * allocation. Should never be more than one of these
	 */

	YAFFS_BLOCK_STATE_FULL,
	/* All the pages in this block have been allocated.
	 */

	YAFFS_BLOCK_STATE_DIRTY,
	/* All pages have been allocated and deleted.
	 * Erase me, reuse me.
	 */

	YAFFS_BLOCK_STATE_CHECKPOINT,
	/* This block is assigned to holding checkpoint data.
	 */

	YAFFS_BLOCK_STATE_COLLECTING,
	/* This block is being garbage collected */

	YAFFS_BLOCK_STATE_DEAD
	/* This block has failed and is not in use */
} yaffs_BlockState;

#define	YAFFS_NUMBER_OF_BLOCK_STATES (YAFFS_BLOCK_STATE_DEAD + 1)


typedef struct {

	int softDeletions:10;	/* number of soft deleted pages */
	int pagesInUse:10;	/* number of pages in use */
	unsigned blockState:4;	/* One of the above block states. NB use unsigned because enum is sometimes an int */
	__u32 needsRetiring:1;	/* Data has failed on this block, need to get valid data off */
				/* and retire the block. */
	__u32 skipErasedCheck:1; /* If this is set we can skip the erased check on this block */
	__u32 gcPrioritise:1; 	/* An ECC check or blank check has failed on this block.
				   It should be prioritised for GC */
	__u32 chunkErrorStrikes:3; /* How many times we've had ecc etc failures on this block and tried to reuse it */

#ifdef CONFIG_YAFFS_YAFFS2
	__u32 hasShrinkHeader:1; /* This block has at least one shrink object header */
	__u32 sequenceNumber;	 /* block sequence number for yaffs2 */
#endif

} yaffs_BlockInfo;

/* -------------------------- Object structure -------------------------------*/
/* This is the object structure as stored on NAND */

typedef struct {
	yaffs_ObjectType type;

	/* Apply to everything  */
	int parentObjectId;
	__u16 sum__NoLongerUsed;        /* checksum of name. No longer used */
	YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];

	/* The following apply to directories, files, symlinks - not hard links */
	__u32 yst_mode;         /* protection */

#ifdef CONFIG_YAFFS_WINCE
	__u32 notForWinCE[5];
#else
	__u32 yst_uid;
	__u32 yst_gid;
	__u32 yst_atime;
	__u32 yst_mtime;
	__u32 yst_ctime;
#endif

	/* File size  applies to files only */
	int fileSize;

	/* Equivalent object id applies to hard links only. */
	int equivalentObjectId;

	/* Alias is for symlinks only. */
	YCHAR alias[YAFFS_MAX_ALIAS_LENGTH + 1];

	__u32 yst_rdev;		/* device stuff for block and char devices (major/min) */

#ifdef CONFIG_YAFFS_WINCE
	__u32 win_ctime[2];
	__u32 win_atime[2];
	__u32 win_mtime[2];
#else
	__u32 roomToGrow[6];

#endif
	__u32 inbandShadowsObject;
	__u32 inbandIsShrink;

	__u32 reservedSpace[2];
	int shadowsObject;	/* This object header shadows the specified object if > 0 */

	/* isShrink applies to object headers written when we shrink the file (ie resize) */
	__u32 isShrink;

} yaffs_ObjectHeader;

/*--------------------------- Tnode -------------------------- */

union yaffs_Tnode_union {
#ifdef CONFIG_YAFFS_TNODE_LIST_DEBUG
	union yaffs_Tnode_union *internal[YAFFS_NTNODES_INTERNAL + 1];
#else
	union yaffs_Tnode_union *internal[YAFFS_NTNODES_INTERNAL];
#endif
/*	__u16 level0[YAFFS_NTNODES_LEVEL0]; */

};

typedef union yaffs_Tnode_union yaffs_Tnode;

struct yaffs_TnodeList_struct {
	struct yaffs_TnodeList_struct *next;
	yaffs_Tnode *tnodes;
};

typedef struct yaffs_TnodeList_struct yaffs_TnodeList;

/*------------------------  Object -----------------------------*/
/* An object can be one of:
 * - a directory (no data, has children links
 * - a regular file (data.... not prunes :->).
 * - a symlink [symbolic link] (the alias).
 * - a hard link
 */

typedef struct {
	__u32 fileSize;
	__u32 scannedFileSize;
	__u32 shrinkSize;
	int topLevel;
	yaffs_Tnode *top;
} yaffs_FileStructure;

typedef struct {
	struct ylist_head children;     /* list of child links */
} yaffs_DirectoryStructure;

typedef struct {
	YCHAR *alias;
} yaffs_SymLinkStructure;

typedef struct {
	struct yaffs_ObjectStruct *equivalentObject;
	__u32 equivalentObjectId;
} yaffs_HardLinkStructure;

typedef union {
	yaffs_FileStructure fileVariant;
	yaffs_DirectoryStructure directoryVariant;
	yaffs_SymLinkStructure symLinkVariant;
	yaffs_HardLinkStructure hardLinkVariant;
} yaffs_ObjectVariant;

struct yaffs_ObjectStruct {
	__u8 deleted:1;		/* This should only apply to unlinked files. */
	__u8 softDeleted:1;	/* it has also been soft deleted */
	__u8 unlinked:1;	/* An unlinked file. The file should be in the unlinked directory.*/
	__u8 fake:1;		/* A fake object has no presence on NAND. */
	__u8 renameAllowed:1;	/* Some objects are not allowed to be renamed. */
	__u8 unlinkAllowed:1;
	__u8 dirty:1;		/* the object needs to be written to flash */
	__u8 valid:1;		/* When the file system is being loaded up, this
				 * object might be created before the data
				 * is available (ie. file data records appear before the header).
				 */
	__u8 lazyLoaded:1;	/* This object has been lazy loaded and is missing some detail */

	__u8 deferedFree:1;	/* For Linux kernel. Object is removed from NAND, but is
				 * still in the inode cache. Free of object is defered.
				 * until the inode is released.
				 */
	__u8 beingCreated:1;	/* This object is still being created so skip some checks. */

	__u8 serial;		/* serial number of chunk in NAND. Cached here */
	__u16 sum;		/* sum of the name to speed searching */

	struct yaffs_DeviceStruct *myDev;       /* The device I'm on */

	struct ylist_head hashLink;     /* list of objects in this hash bucket */

	struct ylist_head hardLinks;    /* all the equivalent hard linked objects */

	/* directory structure stuff */
	/* also used for linking up the free list */
	struct yaffs_ObjectStruct *parent;
	struct ylist_head siblings;

	/* Where's my object header in NAND? */
	int hdrChunk;

	int nDataChunks;	/* Number of data chunks attached to the file. */

	__u32 objectId;		/* the object id value */

	__u32 yst_mode;

#ifdef CONFIG_YAFFS_SHORT_NAMES_IN_RAM
	YCHAR shortName[YAFFS_SHORT_NAME_LENGTH + 1];
#endif

#ifndef __KERNEL__
	__u32 inUse;
#endif

#ifdef CONFIG_YAFFS_WINCE
	__u32 win_ctime[2];
	__u32 win_mtime[2];
	__u32 win_atime[2];
#else
	__u32 yst_uid;
	__u32 yst_gid;
	__u32 yst_atime;
	__u32 yst_mtime;
	__u32 yst_ctime;
#endif

	__u32 yst_rdev;

#ifdef __KERNEL__
	struct inode *myInode;

#endif

	yaffs_ObjectType variantType;

	yaffs_ObjectVariant variant;

};

typedef struct yaffs_ObjectStruct yaffs_Object;

struct yaffs_ObjectList_struct {
	yaffs_Object *objects;
	struct yaffs_ObjectList_struct *next;
};

typedef struct yaffs_ObjectList_struct yaffs_ObjectList;

typedef struct {
	struct ylist_head list;
	int count;
} yaffs_ObjectBucket;


/* yaffs_CheckpointObject holds the definition of an object as dumped
 * by checkpointing.
 */

typedef struct {
	int structType;
	__u32 objectId;
	__u32 parentId;
	int hdrChunk;
	yaffs_ObjectType variantType:3;
	__u8 deleted:1;
	__u8 softDeleted:1;
	__u8 unlinked:1;
	__u8 fake:1;
	__u8 renameAllowed:1;
	__u8 unlinkAllowed:1;
	__u8 serial;

	int nDataChunks;
	__u32 fileSizeOrEquivalentObjectId;
} yaffs_CheckpointObject;

/*--------------------- Temporary buffers ----------------
 *
 * These are chunk-sized working buffers. Each device has a few
 */

typedef struct {
	__u8 *buffer;
	int line;	/* track from whence this buffer was allocated */
	int maxLine;
} yaffs_TempBuffer;

/*----------------- Device ---------------------------------*/

struct yaffs_DeviceStruct {
	struct ylist_head devList;
	const char *name;

	/* Entry parameters set up way early. Yaffs sets up the rest.*/
	int nDataBytesPerChunk;	/* Should be a power of 2 >= 512 */
	int nChunksPerBlock;	/* does not need to be a power of 2 */
	int spareBytesPerChunk;	/* spare area size */
	int startBlock;		/* Start block we're allowed to use */
	int endBlock;		/* End block we're allowed to use */
	int nReservedBlocks;	/* We want this tuneable so that we can reduce */
				/* reserved blocks on NOR and RAM. */


	/* Stuff used by the shared space checkpointing mechanism */
	/* If this value is zero, then this mechanism is disabled */

/*	int nCheckpointReservedBlocks; */ /* Blocks to reserve for checkpoint data */


	int nShortOpCaches;	/* If <= 0, then short op caching is disabled, else
				 * the number of short op caches (don't use too many)
				 */

	int useHeaderFileSize;	/* Flag to determine if we should use file sizes from the header */

	int useNANDECC;		/* Flag to decide whether or not to use NANDECC */

	void *genericDevice;	/* Pointer to device context
				 * On an mtd this holds the mtd pointer.
				 */
	void *superBlock;

	/* NAND access functions (Must be set before calling YAFFS)*/

	int (*writeChunkToNAND) (struct yaffs_DeviceStruct *dev,
					int chunkInNAND, const __u8 *data,
					const yaffs_Spare *spare);
	int (*readChunkFromNAND) (struct yaffs_DeviceStruct *dev,
					int chunkInNAND, __u8 *data,
					yaffs_Spare *spare);
	int (*eraseBlockInNAND) (struct yaffs_DeviceStruct *dev,
					int blockInNAND);
	int (*initialiseNAND) (struct yaffs_DeviceStruct *dev);
	int (*deinitialiseNAND) (struct yaffs_DeviceStruct *dev);

#ifdef CONFIG_YAFFS_YAFFS2
	int (*writeChunkWithTagsToNAND) (struct yaffs_DeviceStruct *dev,
					 int chunkInNAND, const __u8 *data,
					 const yaffs_ExtendedTags *tags);
	int (*readChunkWithTagsFromNAND) (struct yaffs_DeviceStruct *dev,
					  int chunkInNAND, __u8 *data,
					  yaffs_ExtendedTags *tags);
	int (*markNANDBlockBad) (struct yaffs_DeviceStruct *dev, int blockNo);
	int (*queryNANDBlock) (struct yaffs_DeviceStruct *dev, int blockNo,
			       yaffs_BlockState *state, __u32 *sequenceNumber);
#endif

	int isYaffs2;

	/* The removeObjectCallback function must be supplied by OS flavours that
	 * need it. The Linux kernel does not use this, but yaffs direct does use
	 * it to implement the faster readdir
	 */
	void (*removeObjectCallback)(struct yaffs_ObjectStruct *obj);

	/* Callback to mark the superblock dirsty */
	void (*markSuperBlockDirty)(void *superblock);

	int wideTnodesDisabled; /* Set to disable wide tnodes */

	YCHAR *pathDividers;	/* String of legal path dividers */


	/* End of stuff that must be set before initialisation. */

	/* Checkpoint control. Can be set before or after initialisation */
	__u8 skipCheckpointRead;
	__u8 skipCheckpointWrite;

	/* Runtime parameters. Set up by YAFFS. */

	__u16 chunkGroupBits;	/* 0 for devices <= 32MB. else log2(nchunks) - 16 */
	__u16 chunkGroupSize;	/* == 2^^chunkGroupBits */

	/* Stuff to support wide tnodes */
	__u32 tnodeWidth;
	__u32 tnodeMask;

	/* Stuff for figuring out file offset to chunk conversions */
	__u32 chunkShift; /* Shift value */
	__u32 chunkDiv;   /* Divisor after shifting: 1 for power-of-2 sizes */
	__u32 chunkMask;  /* Mask to use for power-of-2 case */

	/* Stuff to handle inband tags */
	int inbandTags;
	__u32 totalBytesPerChunk;

#ifdef __KERNEL__

	struct semaphore sem;	/* Semaphore for waiting on erasure.*/
	struct semaphore grossLock;	/* Gross locking semaphore */
	__u8 *spareBuffer;	/* For mtdif2 use. Don't know the size of the buffer
				 * at compile time so we have to allocate it.
				 */
	void (*putSuperFunc) (struct super_block *sb);
#endif

	int isMounted;

	int isCheckpointed;


	/* Stuff to support block offsetting to support start block zero */
	int internalStartBlock;
	int internalEndBlock;
	int blockOffset;
	int chunkOffset;


	/* Runtime checkpointing stuff */
	int checkpointPageSequence;   /* running sequence number of checkpoint pages */
	int checkpointByteCount;
	int checkpointByteOffset;
	__u8 *checkpointBuffer;
	int checkpointOpenForWrite;
	int blocksInCheckpoint;
	int checkpointCurrentChunk;
	int checkpointCurrentBlock;
	int checkpointNextBlock;
	int *checkpointBlockList;
	int checkpointMaxBlocks;
	__u32 checkpointSum;
	__u32 checkpointXor;

	int nCheckpointBlocksRequired; /* Number of blocks needed to store current checkpoint set */

	/* Block Info */
	yaffs_BlockInfo *blockInfo;
	__u8 *chunkBits;	/* bitmap of chunks in use */
	unsigned blockInfoAlt:1;	/* was allocated using alternative strategy */
	unsigned chunkBitsAlt:1;	/* was allocated using alternative strategy */
	int chunkBitmapStride;	/* Number of bytes of chunkBits per block.
				 * Must be consistent with nChunksPerBlock.
				 */

	int nErasedBlocks;
	int allocationBlock;	/* Current block being allocated off */
	__u32 allocationPage;
	int allocationBlockFinder;	/* Used to search for next allocation block */

	/* Runtime state */
	int nTnodesCreated;
	yaffs_Tnode *freeTnodes;
	int nFreeTnodes;
	yaffs_TnodeList *allocatedTnodeList;

	int isDoingGC;
	int gcBlock;
	int gcChunk;

	int nObjectsCreated;
	yaffs_Object *freeObjects;
	int nFreeObjects;

	int nHardLinks;

	yaffs_ObjectList *allocatedObjectList;

	yaffs_ObjectBucket objectBucket[YAFFS_NOBJECT_BUCKETS];

	int nFreeChunks;

	int currentDirtyChecker;	/* Used to find current dirtiest block */

	__u32 *gcCleanupList;	/* objects to delete at the end of a GC. */
	int nonAggressiveSkip;	/* GC state/mode */

	/* Statistcs */
	int nPageWrites;
	int nPageReads;
	int nBlockErasures;
	int nErasureFailures;
	int nGCCopies;
	int garbageCollections;
	int passiveGarbageCollections;
	int nRetriedWrites;
	int nRetiredBlocks;
	int eccFixed;
	int eccUnfixed;
	int tagsEccFixed;
	int tagsEccUnfixed;
	int nDeletions;
	int nUnmarkedDeletions;

	int hasPendingPrioritisedGCs; /* We think this device might have pending prioritised gcs */

	/* Special directories */
	yaffs_Object *rootDir;
	yaffs_Object *lostNFoundDir;

	/* Buffer areas for storing data to recover from write failures TODO
	 *      __u8            bufferedData[YAFFS_CHUNKS_PER_BLOCK][YAFFS_BYTES_PER_CHUNK];
	 *      yaffs_Spare bufferedSpare[YAFFS_CHUNKS_PER_BLOCK];
	 */

	int bufferedBlock;	/* Which block is buffered here? */
	int doingBufferedBlockRewrite;

	yaffs_ChunkCache *srCache;
	int srLastUse;

	int cacheHits;

	/* Stuff for background deletion and unlinked files.*/
	yaffs_Object *unlinkedDir;	/* Directory where unlinked and deleted files live. */
	yaffs_Object *deletedDir;	/* Directory where deleted objects are sent to disappear. */
	yaffs_Object *unlinkedDeletion;	/* Current file being background deleted.*/
	int nDeletedFiles;		/* Count of files awaiting deletion;*/
	int nUnlinkedFiles;		/* Count of unlinked files. */
	int nBackgroundDeletions;	/* Count of background deletions. */


	/* Temporary buffer management */
	yaffs_TempBuffer tempBuffer[YAFFS_N_TEMP_BUFFERS];
	int maxTemp;
	int tempInUse;
	int unmanagedTempAllocations;
	int unmanagedTempDeallocations;

	/* yaffs2 runtime stuff */
	unsigned sequenceNumber;	/* Sequence number of currently allocating block */
	unsigned oldestDirtySequence;

};

typedef struct yaffs_DeviceStruct yaffs_Device;

/* The static layout of block usage etc is stored in the super block header */
typedef struct {
	int StructType;
	int version;
	int checkpointStartBlock;
	int checkpointEndBlock;
	int startBlock;
	int endBlock;
	int rfu[100];
} yaffs_SuperBlockHeader;

/* The CheckpointDevice structure holds the device information that changes at runtime and
 * must be preserved over unmount/mount cycles.
 */
typedef struct {
	int structType;
	int nErasedBlocks;
	int allocationBlock;	/* Current block being allocated off */
	__u32 allocationPage;
	int nFreeChunks;

	int nDeletedFiles;		/* Count of files awaiting deletion;*/
	int nUnlinkedFiles;		/* Count of unlinked files. */
	int nBackgroundDeletions;	/* Count of background deletions. */

	/* yaffs2 runtime stuff */
	unsigned sequenceNumber;	/* Sequence number of currently allocating block */
	unsigned oldestDirtySequence;

} yaffs_CheckpointDevice;


typedef struct {
	int structType;
	__u32 magic;
	__u32 version;
	__u32 head;
} yaffs_CheckpointValidity;


/*----------------------- YAFFS Functions -----------------------*/

int yaffs_GutsInitialise(yaffs_Device *dev);
void yaffs_Deinitialise(yaffs_Device *dev);

int yaffs_GetNumberOfFreeChunks(yaffs_Device *dev);

int yaffs_RenameObject(yaffs_Object *oldDir, const YCHAR *oldName,
		       yaffs_Object *newDir, const YCHAR *newName);

int yaffs_Unlink(yaffs_Object *dir, const YCHAR *name);
int yaffs_DeleteObject(yaffs_Object *obj);

int yaffs_GetObjectName(yaffs_Object *obj, YCHAR *name, int buffSize);
int yaffs_GetObjectFileLength(yaffs_Object *obj);
int yaffs_GetObjectInode(yaffs_Object *obj);
unsigned yaffs_GetObjectType(yaffs_Object *obj);
int yaffs_GetObjectLinkCount(yaffs_Object *obj);

int yaffs_SetAttributes(yaffs_Object *obj, struct iattr *attr);
int yaffs_GetAttributes(yaffs_Object *obj, struct iattr *attr);

/* File operations */
int yaffs_ReadDataFromFile(yaffs_Object *obj, __u8 *buffer, loff_t offset,
				int nBytes);
int yaffs_WriteDataToFile(yaffs_Object *obj, const __u8 *buffer, loff_t offset,
				int nBytes, int writeThrough);
int yaffs_ResizeFile(yaffs_Object *obj, loff_t newSize);

yaffs_Object *yaffs_MknodFile(yaffs_Object *parent, const YCHAR *name,
				__u32 mode, __u32 uid, __u32 gid);
int yaffs_FlushFile(yaffs_Object *obj, int updateTime);

/* Flushing and checkpointing */
void yaffs_FlushEntireDeviceCache(yaffs_Device *dev);

int yaffs_CheckpointSave(yaffs_Device *dev);
int yaffs_CheckpointRestore(yaffs_Device *dev);

/* Directory operations */
yaffs_Object *yaffs_MknodDirectory(yaffs_Object *parent, const YCHAR *name,
				__u32 mode, __u32 uid, __u32 gid);
yaffs_Object *yaffs_FindObjectByName(yaffs_Object *theDir, const YCHAR *name);
int yaffs_ApplyToDirectoryChildren(yaffs_Object *theDir,
				   int (*fn) (yaffs_Object *));

yaffs_Object *yaffs_FindObjectByNumber(yaffs_Device *dev, __u32 number);

/* Link operations */
yaffs_Object *yaffs_Link(yaffs_Object *parent, const YCHAR *name,
			 yaffs_Object *equivalentObject);

yaffs_Object *yaffs_GetEquivalentObject(yaffs_Object *obj);

/* Symlink operations */
yaffs_Object *yaffs_MknodSymLink(yaffs_Object *parent, const YCHAR *name,
				 __u32 mode, __u32 uid, __u32 gid,
				 const YCHAR *alias);
YCHAR *yaffs_GetSymlinkAlias(yaffs_Object *obj);

/* Special inodes (fifos, sockets and devices) */
yaffs_Object *yaffs_MknodSpecial(yaffs_Object *parent, const YCHAR *name,
				 __u32 mode, __u32 uid, __u32 gid, __u32 rdev);

/* Special directories */
yaffs_Object *yaffs_Root(yaffs_Device *dev);
yaffs_Object *yaffs_LostNFound(yaffs_Device *dev);

#ifdef CONFIG_YAFFS_WINCE
/* CONFIG_YAFFS_WINCE special stuff */
void yfsd_WinFileTimeNow(__u32 target[2]);
#endif

#ifdef __KERNEL__

void yaffs_HandleDeferedFree(yaffs_Object *obj);
#endif

/* Debug dump  */
int yaffs_DumpObject(yaffs_Object *obj);

void yaffs_GutsTest(yaffs_Device *dev);

/* A few useful functions */
void yaffs_InitialiseTags(yaffs_ExtendedTags *tags);
void yaffs_DeleteChunk(yaffs_Device *dev, int chunkId, int markNAND, int lyn);
int yaffs_CheckFF(__u8 *buffer, int nBytes);
void yaffs_HandleChunkError(yaffs_Device *dev, yaffs_BlockInfo *bi);

__u8 *yaffs_GetTempBuffer(yaffs_Device *dev, int lineNo);
void yaffs_ReleaseTempBuffer(yaffs_Device *dev, __u8 *buffer, int lineNo);

#endif
