/*
 * This file is separate from sdb.h, because I want that one to remain
 * unchanged (as far as possible) from the official sdb distribution
 *
 * This file and associated functionality are a playground for me to
 * understand stuff which will later be implemented in more generic places.
 */
#include <linux/sdb.h>

/* This is the union of all currently defined types */
union sdb_record {
	struct sdb_interconnect ic;
	struct sdb_device dev;
	struct sdb_bridge bridge;
	struct sdb_integration integr;
	struct sdb_empty empty;
};

struct fmc_device;

/* Every sdb table is turned into this structure */
struct sdb_array {
	int len;
	int level;
	unsigned long baseaddr;
	struct fmc_device *fmc;		/* the device that hosts it */
	struct sdb_array *parent;	/* NULL at root */
	union sdb_record *record;	/* copies of the struct */
	struct sdb_array **subtree;	/* only valid for bridge items */
};

extern int fmc_scan_sdb_tree(struct fmc_device *fmc, unsigned long address);
extern void fmc_show_sdb_tree(const struct fmc_device *fmc);
extern signed long fmc_find_sdb_device(struct sdb_array *tree, uint64_t vendor,
				       uint32_t device, unsigned long *sz);
extern int fmc_free_sdb_tree(struct fmc_device *fmc);
