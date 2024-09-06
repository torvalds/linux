/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__SPI_BITBANG_H
#define	__SPI_BITBANG_H

#include <linux/workqueue.h>

typedef u32 (*spi_bb_txrx_word_fn)(struct spi_device *, unsigned int, u32, u8, unsigned int);

struct spi_bitbang {
	struct mutex		lock;
	u8			busy;
	u8			use_dma;
	u16			flags;		/* extra spi->mode support */

	struct spi_controller	*ctlr;

	/* setup_transfer() changes clock and/or wordsize to match settings
	 * for this transfer; zeroes restore defaults from spi_device.
	 */
	int	(*setup_transfer)(struct spi_device *spi,
			struct spi_transfer *t);

	void	(*chipselect)(struct spi_device *spi, int is_on);
#define	BITBANG_CS_ACTIVE	1	/* normally nCS, active low */
#define	BITBANG_CS_INACTIVE	0

	/* txrx_bufs() may handle dma mapping for transfers that don't
	 * already have one (transfer.{tx,rx}_dma is zero), or use PIO
	 */
	int	(*txrx_bufs)(struct spi_device *spi, struct spi_transfer *t);

	/* txrx_word[SPI_MODE_*]() just looks like a shift register */
	spi_bb_txrx_word_fn txrx_word[SPI_MODE_X_MASK + 1];

	int	(*set_line_direction)(struct spi_device *spi, bool output);
};

/* you can call these default bitbang->master methods from your custom
 * methods, if you like.
 */
extern int spi_bitbang_setup(struct spi_device *spi);
extern void spi_bitbang_cleanup(struct spi_device *spi);
extern int spi_bitbang_setup_transfer(struct spi_device *spi,
				      struct spi_transfer *t);

/* start or stop queue processing */
extern int spi_bitbang_start(struct spi_bitbang *spi);
extern int spi_bitbang_init(struct spi_bitbang *spi);
extern void spi_bitbang_stop(struct spi_bitbang *spi);

#endif	/* __SPI_BITBANG_H */
