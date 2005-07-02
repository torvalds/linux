#include <linux/pagemap.h>
#include <linux/blkdev.h>

/*
 * add_gd_partition adds a partitions details to the devices partition
 * description.
 */
enum { MAX_PART = 256 };

struct parsed_partitions {
	char name[BDEVNAME_SIZE];
	struct {
		sector_t from;
		sector_t size;
		int flags;
	} parts[MAX_PART];
	int next;
	int limit;
};

static inline void
put_partition(struct parsed_partitions *p, int n, sector_t from, sector_t size)
{
	if (n < p->limit) {
		p->parts[n].from = from;
		p->parts[n].size = size;
		printk(" %s%d", p->name, n);
	}
}

extern int warn_no_part;

