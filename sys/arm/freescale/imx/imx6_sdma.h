/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define	SDMAARM_MC0PTR		0x00	/* ARM platform Channel 0 Pointer */
#define	SDMAARM_INTR		0x04	/* Channel Interrupts */
#define	SDMAARM_STOP_STAT	0x08	/* Channel Stop/Channel Status */
#define	SDMAARM_HSTART		0x0C	/* Channel Start */
#define	SDMAARM_EVTOVR		0x10	/* Channel Event Override */
#define	SDMAARM_DSPOVR		0x14	/* Channel BP Override */
#define	SDMAARM_HOSTOVR		0x18	/* Channel ARM platform Override */
#define	SDMAARM_EVTPEND		0x1C	/* Channel Event Pending */
#define	SDMAARM_RESET		0x24	/* Reset Register */
#define	SDMAARM_EVTERR		0x28	/* DMA Request Error Register */
#define	SDMAARM_INTRMASK	0x2C	/* Channel ARM platform Interrupt Mask */
#define	SDMAARM_PSW		0x30	/* Schedule Status */
#define	SDMAARM_EVTERRDBG	0x34	/* DMA Request Error Register */
#define	SDMAARM_CONFIG		0x38	/* Configuration Register */
#define	 CONFIG_CSM		0x3
#define	SDMAARM_SDMA_LOCK	0x3C	/* SDMA LOCK */
#define	SDMAARM_ONCE_ENB	0x40	/* OnCE Enable */
#define	SDMAARM_ONCE_DATA	0x44	/* OnCE Data Register */
#define	SDMAARM_ONCE_INSTR	0x48	/* OnCE Instruction Register */
#define	SDMAARM_ONCE_STAT	0x4C	/* OnCE Status Register */
#define	SDMAARM_ONCE_CMD	0x50	/* OnCE Command Register */
#define	SDMAARM_ILLINSTADDR	0x58	/* Illegal Instruction Trap Address */
#define	SDMAARM_CHN0ADDR	0x5C	/* Channel 0 Boot Address */
#define	SDMAARM_EVT_MIRROR	0x60	/* DMA Requests */
#define	SDMAARM_EVT_MIRROR2	0x64	/* DMA Requests 2 */
#define	SDMAARM_XTRIG_CONF1	0x70	/* Cross-Trigger Events Configuration Register 1 */
#define	SDMAARM_XTRIG_CONF2	0x74	/* Cross-Trigger Events Configuration Register 2 */
#define	SDMAARM_SDMA_CHNPRI(n)	(0x100 + 0x4 * n)	/* Channel Priority Registers */
#define	SDMAARM_CHNENBL(n)	(0x200 + 0x4 * n)	/* Channel Enable RAM */

/* SDMA Event Mappings */
#define	SSI1_RX_1	35
#define	SSI1_TX_1	36
#define	SSI1_RX_0	37
#define	SSI1_TX_0	38
#define	SSI2_RX_1	39
#define	SSI2_TX_1	40
#define	SSI2_RX_0	41
#define	SSI2_TX_0	42
#define	SSI3_RX_1	43
#define	SSI3_TX_1	44
#define	SSI3_RX_0	45
#define	SSI3_TX_0	46

#define	C0_ADDR		0x01
#define	C0_LOAD		0x02
#define	C0_DUMP		0x03
#define	C0_SETCTX	0x07
#define	C0_GETCTX	0x03
#define	C0_SETDM	0x01
#define	C0_SETPM	0x04
#define	C0_GETDM	0x02
#define	C0_GETPM	0x08

#define	BD_DONE		0x01
#define	BD_WRAP		0x02
#define	BD_CONT		0x04
#define	BD_INTR		0x08
#define	BD_RROR		0x10
#define	BD_LAST		0x20
#define	BD_EXTD		0x80

/* sDMA data transfer length */
#define	CMD_4BYTES	0
#define	CMD_3BYTES	3
#define	CMD_2BYTES	2
#define	CMD_1BYTES	1

struct sdma_firmware_header {
	uint32_t	magic;
	uint32_t	version_major;
	uint32_t	version_minor;
	uint32_t	script_addrs_start;
	uint32_t	num_script_addrs;
	uint32_t	ram_code_start;
	uint32_t	ram_code_size;
};

struct sdma_mode_count {
	uint16_t count;
	uint8_t status;
	uint8_t command;
};

struct sdma_buffer_descriptor {
	struct sdma_mode_count	mode;
	uint32_t		buffer_addr;
	uint32_t		ext_buffer_addr;
} __packed;

struct sdma_channel_control {
	uint32_t	current_bd_ptr;
	uint32_t	base_bd_ptr;
	uint32_t	unused[2];
} __packed;

struct sdma_state_registers {
	uint32_t	pc     :14;
	uint32_t	unused1: 1;
	uint32_t	t      : 1;
	uint32_t	rpc    :14;
	uint32_t	unused0: 1;
	uint32_t	sf     : 1;
	uint32_t	spc    :14;
	uint32_t	unused2: 1;
	uint32_t	df     : 1;
	uint32_t	epc    :14;
	uint32_t	lm     : 2;
} __packed;

struct sdma_context_data {
	struct sdma_state_registers	channel_state;
	uint32_t	gReg[8];
	uint32_t	mda;
	uint32_t	msa;
	uint32_t	ms;
	uint32_t	md;
	uint32_t	pda;
	uint32_t	psa;
	uint32_t	ps;
	uint32_t	pd;
	uint32_t	ca;
	uint32_t	cs;
	uint32_t	dda;
	uint32_t	dsa;
	uint32_t	ds;
	uint32_t	dd;
	uint32_t	unused[8];
} __packed;

/* SDMA firmware script pointers */
struct sdma_script_start_addrs {
	int32_t	ap_2_ap_addr;
	int32_t	ap_2_bp_addr;
	int32_t	ap_2_ap_fixed_addr;
	int32_t	bp_2_ap_addr;
	int32_t	loopback_on_dsp_side_addr;
	int32_t	mcu_interrupt_only_addr;
	int32_t	firi_2_per_addr;
	int32_t	firi_2_mcu_addr;
	int32_t	per_2_firi_addr;
	int32_t	mcu_2_firi_addr;
	int32_t	uart_2_per_addr;
	int32_t	uart_2_mcu_addr;
	int32_t	per_2_app_addr;
	int32_t	mcu_2_app_addr;
	int32_t	per_2_per_addr;
	int32_t	uartsh_2_per_addr;
	int32_t	uartsh_2_mcu_addr;
	int32_t	per_2_shp_addr;
	int32_t	mcu_2_shp_addr;
	int32_t	ata_2_mcu_addr;
	int32_t	mcu_2_ata_addr;
	int32_t	app_2_per_addr;
	int32_t	app_2_mcu_addr;
	int32_t	shp_2_per_addr;
	int32_t	shp_2_mcu_addr;
	int32_t	mshc_2_mcu_addr;
	int32_t	mcu_2_mshc_addr;
	int32_t	spdif_2_mcu_addr;
	int32_t	mcu_2_spdif_addr;
	int32_t	asrc_2_mcu_addr;
	int32_t	ext_mem_2_ipu_addr;
	int32_t	descrambler_addr;
	int32_t	dptc_dvfs_addr;
	int32_t	utra_addr;
	int32_t	ram_code_start_addr;
	int32_t	mcu_2_ssish_addr;
	int32_t	ssish_2_mcu_addr;
	int32_t	hdmi_dma_addr;
};

#define	SDMA_N_CHANNELS 32
#define	SDMA_N_EVENTS	48
#define	FW_HEADER_MAGIC	0x414d4453

struct sdma_channel {
	struct sdma_conf		*conf;
	struct sdma_buffer_descriptor	*bd;
	uint8_t 			in_use;
};

struct sdma_softc {
	struct resource			*res[2];
	bus_space_tag_t			bst;
	bus_space_handle_t		bsh;
	device_t			dev;
	void				*ih;
	struct sdma_channel_control	*ccb;
	struct sdma_buffer_descriptor	*bd0;
	struct sdma_context_data	*context;
	struct sdma_channel		channel[SDMA_N_CHANNELS];
	uint32_t			num_bd;
	uint32_t			ccb_phys;
	uint32_t			context_phys;
	const struct sdma_firmware_header	*fw_header;
	const struct sdma_script_start_addrs	*fw_scripts;
};

struct sdma_conf {
	bus_addr_t	saddr;
	bus_addr_t	daddr;
	uint32_t	word_length;
	uint32_t	nbits;
	uint32_t	command;
	uint32_t	num_bd;
	uint32_t	event;
	uint32_t	period;
	uint32_t	(*ih)(void *, int);
	void		*ih_user;
};

int sdma_configure(int, struct sdma_conf *);
int sdma_start(int);
int sdma_stop(int);
int sdma_alloc(void);
int sdma_free(int);
