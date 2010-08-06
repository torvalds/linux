/*
 *  fs/partitions/msdos.h
 */

#define MSDOS_LABEL_MAGIC		0xAA55

int msdos_partition(struct parsed_partitions *state);

