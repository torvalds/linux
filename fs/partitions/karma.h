/*
 *  fs/partitions/karma.h
 */

#define KARMA_LABEL_MAGIC		0xAB56

int karma_partition(struct parsed_partitions *state, struct block_device *bdev);

