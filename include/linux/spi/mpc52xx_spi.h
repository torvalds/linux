
#ifndef INCLUDE_MPC5200_SPI_H
#define INCLUDE_MPC5200_SPI_H

extern void mpc52xx_spi_set_premessage_hook(struct spi_master *master,
					    void (*hook)(struct spi_message *m,
							 void *context),
					    void *hook_context);

#endif
