#ifndef MFD_TMIO_H
#define MFD_TMIO_H

/*
 * data for the NAND controller
 */
struct tmio_nand_data {
	struct nand_bbt_descr	*badblock_pattern;
	struct mtd_partition	*partition;
	unsigned int		num_partitions;
};

#define TMIO_NAND_CONFIG	"tmio-nand-config"
#define TMIO_NAND_CONTROL	"tmio-nand-control"
#define TMIO_NAND_IRQ		"tmio-nand"

#endif
