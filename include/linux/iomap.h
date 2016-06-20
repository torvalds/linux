#ifndef LINUX_IOMAP_H
#define LINUX_IOMAP_H 1

#include <linux/types.h>

/* types of block ranges for multipage write mappings. */
#define IOMAP_HOLE	0x01	/* no blocks allocated, need allocation */
#define IOMAP_DELALLOC	0x02	/* delayed allocation blocks */
#define IOMAP_MAPPED	0x03	/* blocks allocated @blkno */
#define IOMAP_UNWRITTEN	0x04	/* blocks allocated @blkno in unwritten state */

#define IOMAP_NULL_BLOCK -1LL	/* blkno is not valid */

struct iomap {
	sector_t	blkno;	/* first sector of mapping */
	loff_t		offset;	/* file offset of mapping, bytes */
	u64		length;	/* length of mapping, bytes */
	int		type;	/* type of mapping */
};

#endif /* LINUX_IOMAP_H */
