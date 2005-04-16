/*
 *  fs/partitions/osf.h
 */

#define DISKLABELMAGIC (0x82564557UL)

int osf_partition(struct parsed_partitions *state, struct block_device *bdev);
