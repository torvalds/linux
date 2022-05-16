/*
 * MTD partitioning layer definitions
 *
 * (C) 2000 Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is GPL
 */

#ifndef MTD_PARTITIONS_H
#define MTD_PARTITIONS_H

#include <linux/types.h>


/*
 * Partition definition structure:
 *
 * An array of struct partition is passed along with a MTD object to
 * mtd_device_register() to create them.
 *
 * For each partition, these fields are available:
 * name: string that will be used to label the partition's MTD device.
 * types: some partitions can be containers using specific format to describe
 *	embedded subpartitions / volumes. E.g. many home routers use "firmware"
 *	partition that contains at least kernel and rootfs. In such case an
 *	extra parser is needed that will detect these dynamic partitions and
 *	report them to the MTD subsystem. If set this property stores an array
 *	of parser names to use when looking for subpartitions.
 * size: the partition size; if defined as MTDPART_SIZ_FULL, the partition
 * 	will extend to the end of the master MTD device.
 * offset: absolute starting position within the master MTD device; if
 * 	defined as MTDPART_OFS_APPEND, the partition will start where the
 *	previous one ended; if MTDPART_OFS_NXTBLK, at the next erase block;
 *	if MTDPART_OFS_RETAIN, consume as much as possible, leaving size
 *	after the end of partition.
 * mask_flags: contains flags that have to be masked (removed) from the
 * 	master MTD flag set for the corresponding MTD partition.
 * 	For example, to force a read-only partition, simply adding
 * 	MTD_WRITEABLE to the mask_flags will do the trick.
 * add_flags: contains flags to add to the parent flags
 *
 * Note: writeable partitions require their size and offset be
 * erasesize aligned (e.g. use MTDPART_OFS_NEXTBLK).
 */

struct mtd_partition {
	const char *name;		/* identifier string */
	const char *const *types;	/* names of parsers to use if any */
	uint64_t size;			/* partition size */
	uint64_t offset;		/* offset within the master MTD space */
	uint32_t mask_flags;		/* master MTD flags to mask out for this partition */
	uint32_t add_flags;		/* flags to add to the partition */
	struct device_node *of_node;
};

#define MTDPART_OFS_RETAIN	(-3)
#define MTDPART_OFS_NXTBLK	(-2)
#define MTDPART_OFS_APPEND	(-1)
#define MTDPART_SIZ_FULL	(0)


struct mtd_info;
struct device_node;

/**
 * struct mtd_part_parser_data - used to pass data to MTD partition parsers.
 * @origin: for RedBoot, start address of MTD device
 */
struct mtd_part_parser_data {
	unsigned long origin;
};


/*
 * Functions dealing with the various ways of partitioning the space
 */

struct mtd_part_parser {
	struct list_head list;
	struct module *owner;
	const char *name;
	const struct of_device_id *of_match_table;
	int (*parse_fn)(struct mtd_info *, const struct mtd_partition **,
			struct mtd_part_parser_data *);
	void (*cleanup)(const struct mtd_partition *pparts, int nr_parts);
};

/* Container for passing around a set of parsed partitions */
struct mtd_partitions {
	const struct mtd_partition *parts;
	int nr_parts;
	const struct mtd_part_parser *parser;
};

extern int __register_mtd_parser(struct mtd_part_parser *parser,
				 struct module *owner);
#define register_mtd_parser(parser) __register_mtd_parser(parser, THIS_MODULE)

extern void deregister_mtd_parser(struct mtd_part_parser *parser);

/*
 * module_mtd_part_parser() - Helper macro for MTD partition parsers that don't
 * do anything special in module init/exit. Each driver may only use this macro
 * once, and calling it replaces module_init() and module_exit().
 */
#define module_mtd_part_parser(__mtd_part_parser) \
	module_driver(__mtd_part_parser, register_mtd_parser, \
		      deregister_mtd_parser)

int mtd_add_partition(struct mtd_info *master, const char *name,
		      long long offset, long long length);
int mtd_del_partition(struct mtd_info *master, int partno);
uint64_t mtd_get_device_size(const struct mtd_info *mtd);

#endif
