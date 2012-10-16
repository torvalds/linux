/* linux/include/linux/spi/spi-dw.h
 *
 * Copyright (C) 2012 Altera Corporation
 *
 * Maintainer: Lee Booi Lim <lblim@altera.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef LINUX_DW_SPI_H
#define LINUX_DW_SPI_H

#include <linux/spi/spi.h>


#define DW_SPI_OF_COMPATIBLE	"snps,dw-spi-mmio"
#define MAX_SPI_DEVICES		16

enum dw_spi_dma_resources {
	DW_SPI_DMA_CH_RX = 0,
	DW_SPI_DMA_CH_TX,
	DW_SPI_DMA_CH_MAX,
};



struct dw_spi_pdata {
	struct resource dwi_spi_dma_res[DW_SPI_DMA_CH_MAX];
};

#endif /* LINUX_DW_SPI_H */
