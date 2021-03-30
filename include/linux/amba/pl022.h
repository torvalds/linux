/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/linux/amba/pl022.h
 *
 * Copyright (C) 2008-2009 ST-Ericsson AB
 * Copyright (C) 2006 STMicroelectronics Pvt. Ltd.
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 *
 * Initial version inspired by:
 *	linux-2.6.17-rc3-mm1/drivers/spi/pxa2xx_spi.c
 * Initial adoption to PL022 by:
 *      Sachin Verma <sachin.verma@st.com>
 */

#ifndef _SSP_PL022_H
#define _SSP_PL022_H

#include <linux/types.h>

/**
 * whether SSP is in loopback mode or not
 */
enum ssp_loopback {
	LOOPBACK_DISABLED,
	LOOPBACK_ENABLED
};

/**
 * enum ssp_interface - interfaces allowed for this SSP Controller
 * @SSP_INTERFACE_MOTOROLA_SPI: Motorola Interface
 * @SSP_INTERFACE_TI_SYNC_SERIAL: Texas Instrument Synchronous Serial
 * interface
 * @SSP_INTERFACE_NATIONAL_MICROWIRE: National Semiconductor Microwire
 * interface
 * @SSP_INTERFACE_UNIDIRECTIONAL: Unidirectional interface (STn8810
 * &STn8815 only)
 */
enum ssp_interface {
	SSP_INTERFACE_MOTOROLA_SPI,
	SSP_INTERFACE_TI_SYNC_SERIAL,
	SSP_INTERFACE_NATIONAL_MICROWIRE,
	SSP_INTERFACE_UNIDIRECTIONAL
};

/**
 * enum ssp_hierarchy - whether SSP is configured as Master or Slave
 */
enum ssp_hierarchy {
	SSP_MASTER,
	SSP_SLAVE
};

/**
 * enum ssp_clock_params - clock parameters, to set SSP clock at a
 * desired freq
 */
struct ssp_clock_params {
	u8 cpsdvsr; /* value from 2 to 254 (even only!) */
	u8 scr;	    /* value from 0 to 255 */
};

/**
 * enum ssp_rx_endian - endianess of Rx FIFO Data
 * this feature is only available in ST versionf of PL022
 */
enum ssp_rx_endian {
	SSP_RX_MSB,
	SSP_RX_LSB
};

/**
 * enum ssp_tx_endian - endianess of Tx FIFO Data
 */
enum ssp_tx_endian {
	SSP_TX_MSB,
	SSP_TX_LSB
};

/**
 * enum ssp_data_size - number of bits in one data element
 */
enum ssp_data_size {
	SSP_DATA_BITS_4 = 0x03, SSP_DATA_BITS_5, SSP_DATA_BITS_6,
	SSP_DATA_BITS_7, SSP_DATA_BITS_8, SSP_DATA_BITS_9,
	SSP_DATA_BITS_10, SSP_DATA_BITS_11, SSP_DATA_BITS_12,
	SSP_DATA_BITS_13, SSP_DATA_BITS_14, SSP_DATA_BITS_15,
	SSP_DATA_BITS_16, SSP_DATA_BITS_17, SSP_DATA_BITS_18,
	SSP_DATA_BITS_19, SSP_DATA_BITS_20, SSP_DATA_BITS_21,
	SSP_DATA_BITS_22, SSP_DATA_BITS_23, SSP_DATA_BITS_24,
	SSP_DATA_BITS_25, SSP_DATA_BITS_26, SSP_DATA_BITS_27,
	SSP_DATA_BITS_28, SSP_DATA_BITS_29, SSP_DATA_BITS_30,
	SSP_DATA_BITS_31, SSP_DATA_BITS_32
};

/**
 * enum ssp_mode - SSP mode of operation (Communication modes)
 */
enum ssp_mode {
	INTERRUPT_TRANSFER,
	POLLING_TRANSFER,
	DMA_TRANSFER
};

/**
 * enum ssp_rx_level_trig - receive FIFO watermark level which triggers
 * IT: Interrupt fires when _N_ or more elements in RX FIFO.
 */
enum ssp_rx_level_trig {
	SSP_RX_1_OR_MORE_ELEM,
	SSP_RX_4_OR_MORE_ELEM,
	SSP_RX_8_OR_MORE_ELEM,
	SSP_RX_16_OR_MORE_ELEM,
	SSP_RX_32_OR_MORE_ELEM
};

/**
 * Transmit FIFO watermark level which triggers (IT Interrupt fires
 * when _N_ or more empty locations in TX FIFO)
 */
enum ssp_tx_level_trig {
	SSP_TX_1_OR_MORE_EMPTY_LOC,
	SSP_TX_4_OR_MORE_EMPTY_LOC,
	SSP_TX_8_OR_MORE_EMPTY_LOC,
	SSP_TX_16_OR_MORE_EMPTY_LOC,
	SSP_TX_32_OR_MORE_EMPTY_LOC
};

/**
 * enum SPI Clock Phase - clock phase (Motorola SPI interface only)
 * @SSP_CLK_FIRST_EDGE: Receive data on first edge transition (actual direction depends on polarity)
 * @SSP_CLK_SECOND_EDGE: Receive data on second edge transition (actual direction depends on polarity)
 */
enum ssp_spi_clk_phase {
	SSP_CLK_FIRST_EDGE,
	SSP_CLK_SECOND_EDGE
};

/**
 * enum SPI Clock Polarity - clock polarity (Motorola SPI interface only)
 * @SSP_CLK_POL_IDLE_LOW: Low inactive level
 * @SSP_CLK_POL_IDLE_HIGH: High inactive level
 */
enum ssp_spi_clk_pol {
	SSP_CLK_POL_IDLE_LOW,
	SSP_CLK_POL_IDLE_HIGH
};

/**
 * Microwire Conrol Lengths Command size in microwire format
 */
enum ssp_microwire_ctrl_len {
	SSP_BITS_4 = 0x03, SSP_BITS_5, SSP_BITS_6,
	SSP_BITS_7, SSP_BITS_8, SSP_BITS_9,
	SSP_BITS_10, SSP_BITS_11, SSP_BITS_12,
	SSP_BITS_13, SSP_BITS_14, SSP_BITS_15,
	SSP_BITS_16, SSP_BITS_17, SSP_BITS_18,
	SSP_BITS_19, SSP_BITS_20, SSP_BITS_21,
	SSP_BITS_22, SSP_BITS_23, SSP_BITS_24,
	SSP_BITS_25, SSP_BITS_26, SSP_BITS_27,
	SSP_BITS_28, SSP_BITS_29, SSP_BITS_30,
	SSP_BITS_31, SSP_BITS_32
};

/**
 * enum Microwire Wait State
 * @SSP_MWIRE_WAIT_ZERO: No wait state inserted after last command bit
 * @SSP_MWIRE_WAIT_ONE: One wait state inserted after last command bit
 */
enum ssp_microwire_wait_state {
	SSP_MWIRE_WAIT_ZERO,
	SSP_MWIRE_WAIT_ONE
};

/**
 * enum ssp_duplex - whether Full/Half Duplex on microwire, only
 * available in the ST Micro variant.
 * @SSP_MICROWIRE_CHANNEL_FULL_DUPLEX: SSPTXD becomes bi-directional,
 *     SSPRXD not used
 * @SSP_MICROWIRE_CHANNEL_HALF_DUPLEX: SSPTXD is an output, SSPRXD is
 *     an input.
 */
enum ssp_duplex {
	SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	SSP_MICROWIRE_CHANNEL_HALF_DUPLEX
};

/**
 * enum ssp_clkdelay - an optional clock delay on the feedback clock
 * only available in the ST Micro PL023 variant.
 * @SSP_FEEDBACK_CLK_DELAY_NONE: no delay, the data coming in from the
 * slave is sampled directly
 * @SSP_FEEDBACK_CLK_DELAY_1T: the incoming slave data is sampled with
 * a delay of T-dt
 * @SSP_FEEDBACK_CLK_DELAY_2T: dito with a delay if 2T-dt
 * @SSP_FEEDBACK_CLK_DELAY_3T: dito with a delay if 3T-dt
 * @SSP_FEEDBACK_CLK_DELAY_4T: dito with a delay if 4T-dt
 * @SSP_FEEDBACK_CLK_DELAY_5T: dito with a delay if 5T-dt
 * @SSP_FEEDBACK_CLK_DELAY_6T: dito with a delay if 6T-dt
 * @SSP_FEEDBACK_CLK_DELAY_7T: dito with a delay if 7T-dt
 */
enum ssp_clkdelay {
	SSP_FEEDBACK_CLK_DELAY_NONE,
	SSP_FEEDBACK_CLK_DELAY_1T,
	SSP_FEEDBACK_CLK_DELAY_2T,
	SSP_FEEDBACK_CLK_DELAY_3T,
	SSP_FEEDBACK_CLK_DELAY_4T,
	SSP_FEEDBACK_CLK_DELAY_5T,
	SSP_FEEDBACK_CLK_DELAY_6T,
	SSP_FEEDBACK_CLK_DELAY_7T
};

/**
 * CHIP select/deselect commands
 */
enum ssp_chip_select {
	SSP_CHIP_SELECT,
	SSP_CHIP_DESELECT
};


struct dma_chan;
/**
 * struct pl022_ssp_master - device.platform_data for SPI controller devices.
 * @bus_id: identifier for this bus
 * @enable_dma: if true enables DMA driven transfers.
 * @dma_rx_param: parameter to locate an RX DMA channel.
 * @dma_tx_param: parameter to locate a TX DMA channel.
 * @autosuspend_delay: delay in ms following transfer completion before the
 *     runtime power management system suspends the device. A setting of 0
 *     indicates no delay and the device will be suspended immediately.
 * @rt: indicates the controller should run the message pump with realtime
 *     priority to minimise the transfer latency on the bus.
 */
struct pl022_ssp_controller {
	u16 bus_id;
	u8 enable_dma:1;
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
	void *dma_rx_param;
	void *dma_tx_param;
	int autosuspend_delay;
	bool rt;
};

/**
 * struct ssp_config_chip - spi_board_info.controller_data for SPI
 * slave devices, copied to spi_device.controller_data.
 *
 * @iface: Interface type(Motorola, TI, Microwire, Universal)
 * @hierarchy: sets whether interface is master or slave
 * @slave_tx_disable: SSPTXD is disconnected (in slave mode only)
 * @clk_freq: Tune freq parameters of SSP(when in master mode)
 * @com_mode: communication mode: polling, Interrupt or DMA
 * @rx_lev_trig: Rx FIFO watermark level (for IT & DMA mode)
 * @tx_lev_trig: Tx FIFO watermark level (for IT & DMA mode)
 * @ctrl_len: Microwire interface: Control length
 * @wait_state: Microwire interface: Wait state
 * @duplex: Microwire interface: Full/Half duplex
 * @clkdelay: on the PL023 variant, the delay in feeback clock cycles
 * before sampling the incoming line
 */
struct pl022_config_chip {
	enum ssp_interface iface;
	enum ssp_hierarchy hierarchy;
	bool slave_tx_disable;
	struct ssp_clock_params clk_freq;
	enum ssp_mode com_mode;
	enum ssp_rx_level_trig rx_lev_trig;
	enum ssp_tx_level_trig tx_lev_trig;
	enum ssp_microwire_ctrl_len ctrl_len;
	enum ssp_microwire_wait_state wait_state;
	enum ssp_duplex duplex;
	enum ssp_clkdelay clkdelay;
};

#endif /* _SSP_PL022_H */
