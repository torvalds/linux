/*	$OpenBSD: opal.h,v 1.19 2021/01/23 12:10:08 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_OPAL_H_
#define _MACHINE_OPAL_H_

/* Tokens. */
#define OPAL_TEST			0
#define OPAL_CONSOLE_WRITE		1
#define OPAL_CONSOLE_READ		2
#define OPAL_RTC_READ			3
#define OPAL_RTC_WRITE			4
#define OPAL_CEC_POWER_DOWN		5
#define OPAL_CEC_REBOOT			6
#define OPAL_HANDLE_INTERRUPT		9
#define OPAL_POLL_EVENTS		10
#define OPAL_PCI_CONFIG_READ_WORD	15
#define OPAL_PCI_CONFIG_WRITE_WORD	18
#define OPAL_SET_XIVE			19
#define OPAL_GET_XIVE			20
#define OPAL_PCI_EEH_FREEZE_STATUS	23
#define OPAL_PCI_EEH_FREEZE_CLEAR	26
#define OPAL_PCI_PHB_MMIO_ENABLE	27
#define OPAL_PCI_SET_PHB_MEM_WINDOW	28
#define OPAL_PCI_MAP_PE_MMIO_WINDOW	29
#define OPAL_PCI_SET_PE			31
#define OPAL_PCI_SET_XIVE_PE		37
#define OPAL_GET_MSI_32			39
#define OPAL_GET_MSI_64			40
#define OPAL_START_CPU			41
#define OPAL_PCI_MAP_PE_DMA_WINDOW	44
#define OPAL_PCI_MAP_PE_DMA_WINDOW_REAL	45
#define OPAL_PCI_RESET			49
#define OPAL_REINIT_CPUS		70
#define OPAL_CHECK_TOKEN		80
#define OPAL_SENSOR_READ		88
#define OPAL_IPMI_SEND			107
#define OPAL_IPMI_RECV			108
#define OPAL_CONSOLE_FLUSH		117
#define OPAL_XIVE_RESET			128
#define OPAL_XIVE_GET_IRQ_INFO		129
#define OPAL_XIVE_GET_IRQ_CONFIG	131
#define OPAL_XIVE_SET_IRQ_CONFIG	131
#define OPAL_XIVE_GET_QUEUE_INFO	132
#define OPAL_XIVE_SET_QUEUE_INFO	133
#define OPAL_XIVE_GET_VP_INFO		137
#define OPAL_XIVE_SET_VP_INFO		138
#define OPAL_XIVE_DUMP			142
#define OPAL_SENSOR_READ_U64		162

/* Return codes. */
#define OPAL_SUCCESS			0
#define OPAL_PARAMETER			-1
#define OPAL_BUSY			-2
#define OPAL_PARTIAL			-3
#define OPAL_CONSTRAINED		-4
#define OPAL_CLOSED			-5
#define OPAL_HARDWARE			-6
#define OPAL_UNSUPPORTED		-7
#define OPAL_PERMISSION			-8
#define OPAL_NO_MEM			-9
#define OPAL_RESOURCE			-10
#define OPAL_INTERNAL_ERROR		-11
#define OPAL_BUSY_EVENT			-12
#define OPAL_HARDWARE_FROZEN		-13
#define OPAL_WRONG_STATE		-14
#define OPAL_ASYNC_COMPLETION		-15
#define OPAL_EMPTY			-16

/* OPAL_POLL_EVENT */
#define OPAL_EVENT_CONSOLE_OUTPUT	0x00000008
#define OPAL_EVENT_CONSOLE_INPUT	0x00000010

/* OPAL_PCI_EEH_FREEZE_CLEAR */
#define OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO 1
#define OPAL_EEH_ACTION_CLEAR_FREEZE_DMA 2
#define OPAL_EEH_ACTION_CLEAR_FREEZE_ALL 3

/* OPAL_PCI_PHB_MMIO_ENABLE */
#define OPAL_M32_WINDOW_TYPE		1
#define OPAL_M64_WINDOW_TYPE		2
#define OPAL_IO_WINDOW_TYPE		3
#define OPAL_DISABLE_M64		0
#define OPAL_ENABLE_M64_SPLIT		1
#define OPAL_ENABLE_M64_NON_SPLIT	2

/* OPAL_PCIE_SET_PE */
#define OPAL_IGNORE_RID_BUS_NUMBER	0
#define OPAL_IGNORE_RID_DEVICE_NUMBER	0
#define OPAL_COMPARE_RID_DEVICE_NUMBER	1
#define OPAL_IGNORE_RID_FUNCTION_NUMBER	0
#define OPAL_COMPARE_RID_FUNCTION_NUMBER 1
#define OPAL_UNMAP_PE			0
#define OPAL_MAP_PE			1

/* OPAL_PCI_RESET */
#define OPAL_RESET_PHB_COMPLETE		1
#define OPAL_RESET_PCI_LINK		2
#define OPAL_RESET_PHB_ERROR		3
#define OPAL_RESET_PCI_HOT		4
#define OPAL_RESET_PCI_FUNDAMENTAL	5
#define OPAL_RESET_PCI_IODA_TABLE	6
#define OPAL_DEASSERT_RESET		0
#define OPAL_ASSERT_RESET		1

/* OPAL_REINIT_CPUS */
#define OPAL_REINIT_CPUS_HILE_BE		0x00000001
#define OPAL_REINIT_CPUS_HILE_LE		0x00000002
#define OPAL_REINIT_CPUS_MMU_HASH		0x00000004
#define OPAL_REINIT_CPUS_MMU_RADIX		0x00000008
#define OPAL_REINIT_CPUS_TM_SUSPEND_DISABLED	0x00000010

/* OPAL_CHECK_TOKEN */
#define OPAL_TOKEN_ABSENT		0
#define OPAL_TOKEN_PRESENT		1

/* OPAL_IPMI_SEND/RECV */
#define OPAL_IPMI_MSG_FORMAT_VERSION_1	1

#ifndef _LOCORE
struct opal_ipmi_msg {
	uint8_t	version;
	uint8_t	netfn;
	uint8_t	cmd;
	uint8_t	data[0];
};
#endif

/* OPAL_XIVE_RESET */
#define OPAL_XIVE_MODE_EMU		0
#define OPAL_XIVE_MODE_EXPL		1

/* OPAL_XIVE_GET_IRQ_INFO */
#define OPAL_XIVE_IRQ_TRIGGER_PAGE	0x00000001
#define OPAL_XIVE_IRQ_STORE_EOI		0x00000002
#define OPAL_XIVE_IRQ_LSI		0x00000004
#define OPAL_XIVE_IRQ_SHIFT_BUG		0x00000008
#define OPAL_XIVE_IRQ_MASK_VIA_FW	0x00000010
#define OPAL_XIVE_IRQ_EOI_VIA_FW	0x00000020

/* OPAL_XIVE_GET_QUEUE_INFO */
#define OPAL_XIVE_EQ_ENABLED		0x00000001
#define OPAL_XIVE_EQ_ALWAYS_NOTIFY	0x00000002
#define OPAL_XIVE_EQ_ESCALATE		0x00000004

/* OPAL_XIVE_GET_VP_INFO */
#define OPAL_XIVE_VP_ENABLED		0x00000001
#define OPAL_XIVE_VP_SINGLE_ESCALATION	0x00000002

/* OPAL_XIVE_DUMP */
#define XIVE_DUMP_TM_HYP	0x00000000
#define XIVE_DUMP_TM_POOL	0x00000001
#define XIVE_DUMP_TM_OS		0x00000002
#define XIVE_DUMP_TM_USER	0x00000003
#define XIVE_DUMP_VP		0x00000004
#define XIVE_DUMP_EMU_STATE	0x00000005

#ifndef _LOCORE

void	*opal_phys(void *);

int64_t	opal_test(uint64_t);
int64_t	opal_console_write(int64_t, int64_t *, const uint8_t *);
int64_t	opal_console_read(int64_t, int64_t *, uint8_t *);
int64_t	opal_rtc_read(uint32_t *, uint64_t *);
int64_t	opal_rtc_write(uint32_t, uint64_t);
int64_t	opal_cec_power_down(uint64_t);
int64_t	opal_cec_reboot(void);
int64_t	opal_handle_interrupt(uint32_t, uint64_t *);
int64_t	opal_poll_events(uint64_t *);
int64_t	opal_pci_config_read_word(uint64_t, uint64_t, uint64_t, uint32_t *);
int64_t	opal_pci_config_write_word(uint64_t, uint64_t, uint64_t, uint32_t);
int64_t	opal_set_xive(uint32_t, uint16_t, uint8_t);
int64_t	opal_get_xive(uint32_t, uint16_t *, uint8_t *);
int64_t	opal_pci_eeh_freeze_status(uint64_t, uint64_t, uint8_t *,
	    uint16_t *, uint64_t *);
int64_t	opal_pci_eeh_freeze_clear(uint64_t, uint64_t, uint64_t);
int64_t	opal_pci_phb_mmio_enable(uint64_t, uint16_t, uint16_t, uint16_t);
int64_t	opal_pci_set_phb_mem_window(uint64_t, uint16_t, uint16_t,
	    uint64_t, uint64_t, uint64_t);
int64_t	opal_pci_map_pe_mmio_window(uint64_t, uint64_t, uint16_t,
	    uint16_t, uint16_t);
int64_t	opal_pci_set_pe(uint64_t, uint64_t, uint64_t, uint8_t, uint8_t,
	    uint8_t, uint8_t);
int64_t	opal_pci_set_xive_pe(uint64_t, uint64_t, uint32_t);
int64_t	opal_get_msi_32(uint64_t, uint32_t, uint32_t, uint8_t,
	    uint32_t *, uint32_t *);
int64_t	opal_get_msi_64(uint64_t, uint32_t, uint32_t, uint8_t,
	    uint64_t *, uint32_t *);
int64_t	opal_start_cpu(uint64_t, uint64_t);
int64_t	opal_pci_map_pe_dma_window(uint64_t, uint64_t, uint16_t, uint16_t,
	    uint64_t, uint64_t, uint64_t);
int64_t	opal_pci_map_pe_dma_window_real(uint64_t, uint64_t, uint16_t,
	    uint64_t, uint64_t);
int64_t	opal_pci_reset(uint64_t, uint8_t, uint8_t);
int64_t	opal_reinit_cpus(uint64_t);
int64_t	opal_check_token(uint64_t);
int64_t	opal_sensor_read(uint32_t, int, uint32_t *);
int64_t	opal_ipmi_send(uint64_t, struct opal_ipmi_msg *, uint64_t);
int64_t	opal_ipmi_recv(uint64_t, struct opal_ipmi_msg *, uint64_t *);
int64_t	opal_console_flush(uint64_t);
int64_t	opal_xive_reset(uint64_t);
int64_t	opal_xive_get_irq_info(uint32_t, uint64_t *, uint64_t *,
	    uint64_t *, uint32_t *, uint32_t *);
int64_t	opal_xive_get_irq_config(uint32_t, uint64_t *, uint8_t *, uint32_t *);
int64_t	opal_xive_set_irq_config(uint32_t, uint64_t, uint8_t, uint32_t);
int64_t	opal_xive_get_queue_info(uint64_t, uint8_t, uint64_t *,
	    uint64_t *, uint64_t *, uint32_t *, uint64_t *);
int64_t	opal_xive_set_queue_info(uint64_t, uint8_t, uint64_t,
	    uint64_t, uint64_t);
int64_t	opal_xive_get_vp_info(uint64_t, uint64_t *, uint64_t *,
	    uint64_t *, uint32_t *);
int64_t	opal_xive_set_vp_info(uint64_t, uint64_t, uint64_t);
int64_t	opal_xive_dump(uint32_t, uint32_t);
int64_t	opal_sensor_read_u64(uint32_t, int, uint64_t *);

void	opal_printf(const char *fmt, ...);

void	*opal_intr_establish(uint64_t, int, int (*)(void *), void *);

#endif

#endif /* _MACHINE_OPAL_H_ */
