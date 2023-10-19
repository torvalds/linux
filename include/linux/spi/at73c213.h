/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Board-specific data used to set up AT73c213 audio DAC driver.
 */

#ifndef __LINUX_SPI_AT73C213_H
#define __LINUX_SPI_AT73C213_H

/**
 * at73c213_board_info - how the external DAC is wired to the device.
 *
 * @ssc_id: SSC platform_driver id the DAC shall use to stream the audio.
 * @dac_clk: the external clock used to provide master clock to the DAC.
 * @shortname: a short discription for the DAC, seen by userspace tools.
 *
 * This struct contains the configuration of the hardware connection to the
 * external DAC. The DAC needs a master clock and a I2S audio stream. It also
 * provides a name which is used to identify it in userspace tools.
 */
struct at73c213_board_info {
	int		ssc_id;
	struct clk	*dac_clk;
	char		shortname[32];
};

#endif /* __LINUX_SPI_AT73C213_H */
