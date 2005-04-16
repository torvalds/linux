/*
 *  fs/partitions/sun.h
 */

#define SUN_LABEL_MAGIC          0xDABE

int sun_partition(struct parsed_partitions *state, struct block_device *bdev);
