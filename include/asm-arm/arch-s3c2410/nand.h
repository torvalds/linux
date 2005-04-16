/* linux/include/asm-arm/arch-s3c2410/nand.h
 *
 * (c) 2004 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - NAND device controller platfrom_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *	23-Sep-2004 BJD  Created file
*/

/* struct s3c2410_nand_set
 *
 * define an set of one or more nand chips registered with an unique mtd
 *
 * nr_chips	 = number of chips in this set
 * nr_partitions = number of partitions pointed to be partitoons (or zero)
 * name		 = name of set (optional)
 * nr_map	 = map for low-layer logical to physical chip numbers (option)
 * partitions	 = mtd partition list
*/

struct s3c2410_nand_set {
	int			nr_chips;
	int			nr_partitions;
	char			*name;
	int			*nr_map;
	struct mtd_partition	*partitions;
};

struct s3c2410_platform_nand {
	/* timing information for controller, all times in nanoseconds */

	int	tacls;	/* time for active CLE/ALE to nWE/nOE */
	int	twrph0;	/* active time for nWE/nOE */
	int	twrph1;	/* time for release CLE/ALE from nWE/nOE inactive */

	int			nr_sets;
	struct s3c2410_nand_set *sets;

	void			(*select_chip)(struct s3c2410_nand_set *,
					       int chip);
};

