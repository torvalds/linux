/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Xilinx SD-FEC
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 IP. It provides a char device
 * in sysfs and supports file operations like open(), close() and ioctl().
 */
#ifndef __XILINX_SDFEC_H__
#define __XILINX_SDFEC_H__

#include <linux/types.h>

/**
 * enum xsdfec_code - Code Type.
 * @XSDFEC_TURBO_CODE: Driver is configured for Turbo mode.
 * @XSDFEC_LDPC_CODE: Driver is configured for LDPC mode.
 *
 * This enum is used to indicate the mode of the driver. The mode is determined
 * by checking which codes are set in the driver. Note that the mode cannot be
 * changed by the driver.
 */
enum xsdfec_code {
	XSDFEC_TURBO_CODE = 0,
	XSDFEC_LDPC_CODE,
};

/**
 * enum xsdfec_order - Order
 * @XSDFEC_MAINTAIN_ORDER: Maintain order execution of blocks.
 * @XSDFEC_OUT_OF_ORDER: Out-of-order execution of blocks.
 *
 * This enum is used to indicate whether the order of blocks can change from
 * input to output.
 */
enum xsdfec_order {
	XSDFEC_MAINTAIN_ORDER = 0,
	XSDFEC_OUT_OF_ORDER,
};

/**
 * enum xsdfec_state - State.
 * @XSDFEC_INIT: Driver is initialized.
 * @XSDFEC_STARTED: Driver is started.
 * @XSDFEC_STOPPED: Driver is stopped.
 * @XSDFEC_NEEDS_RESET: Driver needs to be reset.
 * @XSDFEC_PL_RECONFIGURE: Programmable Logic needs to be recofigured.
 *
 * This enum is used to indicate the state of the driver.
 */
enum xsdfec_state {
	XSDFEC_INIT = 0,
	XSDFEC_STARTED,
	XSDFEC_STOPPED,
	XSDFEC_NEEDS_RESET,
	XSDFEC_PL_RECONFIGURE,
};

/**
 * enum xsdfec_axis_width - AXIS_WIDTH.DIN Setting for 128-bit width.
 * @XSDFEC_1x128b: DIN data input stream consists of a 128-bit lane
 * @XSDFEC_2x128b: DIN data input stream consists of two 128-bit lanes
 * @XSDFEC_4x128b: DIN data input stream consists of four 128-bit lanes
 *
 * This enum is used to indicate the AXIS_WIDTH.DIN setting for 128-bit width.
 * The number of lanes of the DIN data input stream depends upon the
 * AXIS_WIDTH.DIN parameter.
 */
enum xsdfec_axis_width {
	XSDFEC_1x128b = 1,
	XSDFEC_2x128b = 2,
	XSDFEC_4x128b = 4,
};

/**
 * enum xsdfec_axis_word_include - Words Configuration.
 * @XSDFEC_FIXED_VALUE: Fixed, the DIN_WORDS AXI4-Stream interface is removed
 *			from the IP instance and is driven with the specified
 *			number of words.
 * @XSDFEC_IN_BLOCK: In Block, configures the IP instance to expect a single
 *		     DIN_WORDS value per input code block. The DIN_WORDS
 *		     interface is present.
 * @XSDFEC_PER_AXI_TRANSACTION: Per Transaction, configures the IP instance to
 * expect one DIN_WORDS value per input transaction on the DIN interface. The
 * DIN_WORDS interface is present.
 * @XSDFEC_AXIS_WORDS_INCLUDE_MAX: Used to indicate out of bound Words
 *				   Configurations.
 *
 * This enum is used to specify the DIN_WORDS configuration.
 */
enum xsdfec_axis_word_include {
	XSDFEC_FIXED_VALUE = 0,
	XSDFEC_IN_BLOCK,
	XSDFEC_PER_AXI_TRANSACTION,
	XSDFEC_AXIS_WORDS_INCLUDE_MAX,
};

/**
 * struct xsdfec_irq - Enabling or Disabling Interrupts.
 * @enable_isr: If true enables the ISR
 * @enable_ecc_isr: If true enables the ECC ISR
 */
struct xsdfec_irq {
	__s8 enable_isr;
	__s8 enable_ecc_isr;
};

/**
 * struct xsdfec_config - Configuration of SD-FEC core.
 * @code: The codes being used by the SD-FEC instance
 * @order: Order of Operation
 * @din_width: Width of the DIN AXI4-Stream
 * @din_word_include: How DIN_WORDS are inputted
 * @dout_width: Width of the DOUT AXI4-Stream
 * @dout_word_include: HOW DOUT_WORDS are outputted
 * @irq: Enabling or disabling interrupts
 * @bypass: Is the core being bypassed
 * @code_wr_protect: Is write protection of LDPC codes enabled
 */
struct xsdfec_config {
	__u32 code;
	__u32 order;
	__u32 din_width;
	__u32 din_word_include;
	__u32 dout_width;
	__u32 dout_word_include;
	struct xsdfec_irq irq;
	__s8 bypass;
	__s8 code_wr_protect;
};

/*
 * XSDFEC IOCTL List
 */
#define XSDFEC_MAGIC 'f'
#endif /* __XILINX_SDFEC_H__ */
