/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (C) 2023 OpenSynergy GmbH
 * Copyright (C) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _LINUX_VIRTIO_VIRTIO_SPI_H
#define _LINUX_VIRTIO_VIRTIO_SPI_H

#include <linux/types.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_types.h>

/* Sample data on trailing clock edge */
#define VIRTIO_SPI_CPHA			_BITUL(0)
/* Clock is high when IDLE */
#define VIRTIO_SPI_CPOL			_BITUL(1)
/* Chip Select is active high */
#define VIRTIO_SPI_CS_HIGH			_BITUL(2)
/* Transmit LSB first */
#define VIRTIO_SPI_MODE_LSB_FIRST		_BITUL(3)
/* Loopback mode */
#define VIRTIO_SPI_MODE_LOOP			_BITUL(4)

/**
 * struct virtio_spi_config - All config fields are read-only for the
 * Virtio SPI driver
 * @cs_max_number: maximum number of chipselect the host SPI controller
 *   supports.
 * @cs_change_supported: indicates if the host SPI controller supports to toggle
 *   chipselect after each transfer in one message:
 *   0: unsupported, chipselect will be kept in active state throughout the
 *      message transaction;
 *   1: supported.
 *   Note: Message here contains a sequence of SPI transfers.
 * @tx_nbits_supported: indicates the supported number of bit for writing:
 *   bit 0: DUAL (2-bit transfer), 1 for supported
 *   bit 1: QUAD (4-bit transfer), 1 for supported
 *   bit 2: OCTAL (8-bit transfer), 1 for supported
 *   other bits are reserved as 0, 1-bit transfer is always supported.
 * @rx_nbits_supported: indicates the supported number of bit for reading:
 *   bit 0: DUAL (2-bit transfer), 1 for supported
 *   bit 1: QUAD (4-bit transfer), 1 for supported
 *   bit 2: OCTAL (8-bit transfer), 1 for supported
 *   other bits are reserved as 0, 1-bit transfer is always supported.
 * @bits_per_word_mask: mask indicating which values of bits_per_word are
 *   supported. If not set, no limitation for bits_per_word.
 * @mode_func_supported: indicates the following features are supported or not:
 *   bit 0-1: CPHA feature
 *     0b00: invalid, should support as least one CPHA setting
 *     0b01: supports CPHA=0 only
 *     0b10: supports CPHA=1 only
 *     0b11: supports CPHA=0 and CPHA=1.
 *   bit 2-3: CPOL feature
 *     0b00: invalid, should support as least one CPOL setting
 *     0b01: supports CPOL=0 only
 *     0b10: supports CPOL=1 only
 *     0b11: supports CPOL=0 and CPOL=1.
 *   bit 4: chipselect active high feature, 0 for unsupported and 1 for
 *     supported, chipselect active low is supported by default.
 *   bit 5: LSB first feature, 0 for unsupported and 1 for supported,
 *     MSB first is supported by default.
 *   bit 6: loopback mode feature, 0 for unsupported and 1 for supported,
 *     normal mode is supported by default.
 * @max_freq_hz: the maximum clock rate supported in Hz unit, 0 means no
 *   limitation for transfer speed.
 * @max_word_delay_ns: the maximum word delay supported, in nanoseconds.
 *   A value of 0 indicates that word delay is unsupported.
 *   Each transfer may consist of a sequence of words.
 * @max_cs_setup_ns: the maximum delay supported after chipselect is asserted,
 *   in ns unit, 0 means delay is not supported to introduce after chipselect is
 *   asserted.
 * @max_cs_hold_ns: the maximum delay supported before chipselect is deasserted,
 *   in ns unit, 0 means delay is not supported to introduce before chipselect
 *   is deasserted.
 * @max_cs_incative_ns: maximum delay supported after chipselect is deasserted,
 *   in ns unit, 0 means delay is not supported to introduce after chipselect is
 *   deasserted.
 */
struct virtio_spi_config {
	__u8 cs_max_number;
	__u8 cs_change_supported;
#define VIRTIO_SPI_RX_TX_SUPPORT_DUAL		_BITUL(0)
#define VIRTIO_SPI_RX_TX_SUPPORT_QUAD		_BITUL(1)
#define VIRTIO_SPI_RX_TX_SUPPORT_OCTAL		_BITUL(2)
	__u8 tx_nbits_supported;
	__u8 rx_nbits_supported;
	__le32 bits_per_word_mask;
#define VIRTIO_SPI_MF_SUPPORT_CPHA_0		_BITUL(0)
#define VIRTIO_SPI_MF_SUPPORT_CPHA_1		_BITUL(1)
#define VIRTIO_SPI_MF_SUPPORT_CPOL_0		_BITUL(2)
#define VIRTIO_SPI_MF_SUPPORT_CPOL_1		_BITUL(3)
#define VIRTIO_SPI_MF_SUPPORT_CS_HIGH		_BITUL(4)
#define VIRTIO_SPI_MF_SUPPORT_LSB_FIRST		_BITUL(5)
#define VIRTIO_SPI_MF_SUPPORT_LOOPBACK		_BITUL(6)
	__le32 mode_func_supported;
	__le32 max_freq_hz;
	__le32 max_word_delay_ns;
	__le32 max_cs_setup_ns;
	__le32 max_cs_hold_ns;
	__le32 max_cs_inactive_ns;
};

/**
 * struct spi_transfer_head - virtio SPI transfer descriptor
 * @chip_select_id: chipselect index the SPI transfer used.
 * @bits_per_word: the number of bits in each SPI transfer word.
 * @cs_change: whether to deselect device after finishing this transfer
 *     before starting the next transfer, 0 means cs keep asserted and
 *     1 means cs deasserted then asserted again.
 * @tx_nbits: bus width for write transfer.
 *     0,1: bus width is 1, also known as SINGLE
 *     2  : bus width is 2, also known as DUAL
 *     4  : bus width is 4, also known as QUAD
 *     8  : bus width is 8, also known as OCTAL
 *     other values are invalid.
 * @rx_nbits: bus width for read transfer.
 *     0,1: bus width is 1, also known as SINGLE
 *     2  : bus width is 2, also known as DUAL
 *     4  : bus width is 4, also known as QUAD
 *     8  : bus width is 8, also known as OCTAL
 *     other values are invalid.
 * @reserved: for future use.
 * @mode: SPI transfer mode.
 *     bit 0: CPHA, determines the timing (i.e. phase) of the data
 *         bits relative to the clock pulses.For CPHA=0, the
 *         "out" side changes the data on the trailing edge of the
 *         preceding clock cycle, while the "in" side captures the data
 *         on (or shortly after) the leading edge of the clock cycle.
 *         For CPHA=1, the "out" side changes the data on the leading
 *         edge of the current clock cycle, while the "in" side
 *         captures the data on (or shortly after) the trailing edge of
 *         the clock cycle.
 *     bit 1: CPOL, determines the polarity of the clock. CPOL=0 is a
 *         clock which idles at 0, and each cycle consists of a pulse
 *         of 1. CPOL=1 is a clock which idles at 1, and each cycle
 *         consists of a pulse of 0.
 *     bit 2: CS_HIGH, if 1, chip select active high, else active low.
 *     bit 3: LSB_FIRST, determines per-word bits-on-wire, if 0, MSB
 *         first, else LSB first.
 *     bit 4: LOOP, loopback mode.
 * @freq: the transfer speed in Hz.
 * @word_delay_ns: delay to be inserted between consecutive words of a
 *     transfer, in ns unit.
 * @cs_setup_ns: delay to be introduced after CS is asserted, in ns
 *     unit.
 * @cs_delay_hold_ns: delay to be introduced before CS is deasserted
 *     for each transfer, in ns unit.
 * @cs_change_delay_inactive_ns: delay to be introduced after CS is
 *     deasserted and before next asserted, in ns unit.
 */
struct spi_transfer_head {
	__u8 chip_select_id;
	__u8 bits_per_word;
	__u8 cs_change;
	__u8 tx_nbits;
	__u8 rx_nbits;
	__u8 reserved[3];
	__le32 mode;
	__le32 freq;
	__le32 word_delay_ns;
	__le32 cs_setup_ns;
	__le32 cs_delay_hold_ns;
	__le32 cs_change_delay_inactive_ns;
};

/**
 * struct spi_transfer_result - virtio SPI transfer result
 * @result: Transfer result code.
 *          VIRTIO_SPI_TRANS_OK: Transfer successful.
 *          VIRTIO_SPI_PARAM_ERR: Parameter error.
 *          VIRTIO_SPI_TRANS_ERR: Transfer error.
 */
struct spi_transfer_result {
#define VIRTIO_SPI_TRANS_OK	0
#define VIRTIO_SPI_PARAM_ERR	1
#define VIRTIO_SPI_TRANS_ERR	2
	__u8 result;
};

#endif /* #ifndef _LINUX_VIRTIO_VIRTIO_SPI_H */
