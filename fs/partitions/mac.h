/*
 *  fs/partitions/mac.h
 */

#define MAC_PARTITION_MAGIC	0x504d

/* type field value for A/UX or other Unix partitions */
#define APPLE_AUX_TYPE	"Apple_UNIX_SVR2"

struct mac_partition {
	__be16	signature;	/* expected to be MAC_PARTITION_MAGIC */
	__be16	res1;
	__be32	map_count;	/* # blocks in partition map */
	__be32	start_block;	/* absolute starting block # of partition */
	__be32	block_count;	/* number of blocks in partition */
	char	name[32];	/* partition name */
	char	type[32];	/* string type description */
	__be32	data_start;	/* rel block # of first data block */
	__be32	data_count;	/* number of data blocks */
	__be32	status;		/* partition status bits */
	__be32	boot_start;
	__be32	boot_size;
	__be32	boot_load;
	__be32	boot_load2;
	__be32	boot_entry;
	__be32	boot_entry2;
	__be32	boot_cksum;
	char	processor[16];	/* identifies ISA of boot */
	/* there is more stuff after this that we don't need */
};

#define MAC_STATUS_BOOTABLE	8	/* partition is bootable */

#define MAC_DRIVER_MAGIC	0x4552

/* Driver descriptor structure, in block 0 */
struct mac_driver_desc {
	__be16	signature;	/* expected to be MAC_DRIVER_MAGIC */
	__be16	block_size;
	__be32	block_count;
    /* ... more stuff */
};

int mac_partition(struct parsed_partitions *state, struct block_device *bdev);
