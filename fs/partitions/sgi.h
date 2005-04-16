/*
 *  fs/partitions/sgi.h
 */

extern int sgi_partition(struct parsed_partitions *state, struct block_device *bdev);

#define SGI_LABEL_MAGIC 0x0be5a941

