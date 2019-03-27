/*-
 * Copyright (c) 2015 Nathan Whitehorn
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

#ifndef _POWERNV_OPAL_H
#define _POWERNV_OPAL_H

#include <sys/cdefs.h>
#include <sys/types.h>

/* Check if OPAL is correctly instantiated. Will try to instantiate it. */
int opal_check(void);

/* Call an OPAL method. Any pointers passed must be real-mode accessible! */
int opal_call(uint64_t token, ...);

#define OPAL_CONSOLE_WRITE		1
#define OPAL_CONSOLE_READ		2
#define OPAL_RTC_READ			3
#define OPAL_RTC_WRITE			4
#define	OPAL_CEC_POWER_DOWN		5
#define	OPAL_CEC_REBOOT			6
#define	OPAL_HANDLE_INTERRUPT		9
#define	OPAL_POLL_EVENTS		10
#define	OPAL_PCI_CONFIG_READ_BYTE	13
#define	OPAL_PCI_CONFIG_READ_HALF_WORD	14
#define	OPAL_PCI_CONFIG_READ_WORD	15
#define	OPAL_PCI_CONFIG_WRITE_BYTE	16
#define	OPAL_PCI_CONFIG_WRITE_HALF_WORD	17
#define	OPAL_PCI_CONFIG_WRITE_WORD	18
#define	OPAL_PCI_EEH_FREEZE_CLEAR	26
#define	OPAL_PCI_PHB_MMIO_ENABLE	27
#define	OPAL_PCI_SET_PHB_MEM_WINDOW	28
#define	OPAL_PCI_MAP_PE_MMIO_WINDOW	29
#define	OPAL_PCI_SET_XIVE_PE		37
#define	OPAL_PCI_RESET			49
#define	OPAL_PCI_POLL			62
#define	OPAL_SET_XIVE           	19
#define	OPAL_GET_XIVE           	20
#define	OPAL_PCI_SET_PE			31
#define	OPAL_GET_MSI_32			39
#define	OPAL_GET_MSI_64			40
#define	OPAL_PCI_MSI_EOI		63
#define	OPAL_PCI_GET_PHB_DIAG_DATA2	64
#define	OPAL_START_CPU			41
#define	OPAL_PCI_MAP_PE_DMA_WINDOW	44
#define	OPAL_PCI_MAP_PE_DMA_WINDOW_REAL	45
#define	OPAL_RETURN_CPU			69
#define	OPAL_REINIT_CPUS		70
#define	OPAL_CHECK_TOKEN		80
#define	OPAL_CHECK_ASYNC_COMPLETION	86
#define	OPAL_SENSOR_READ		88
#define	OPAL_HANDLE_HMI			98
#define	OPAL_IPMI_SEND			107
#define	OPAL_IPMI_RECV			108
#define	OPAL_I2C_REQUEST		109
#define	OPAL_FLASH_READ			110
#define	OPAL_FLASH_WRITE		111
#define	OPAL_FLASH_ERASE		111
#define	OPAL_INT_GET_XIRR		122
#define	OPAL_INT_SET_CPPR		123
#define	OPAL_INT_EOI			124
#define	OPAL_INT_SET_MFRR		125
#define	OPAL_PCI_TCE_KILL		126
#define	OPAL_XIVE_RESET			128
#define	OPAL_XIVE_GET_IRQ_INFO		129
#define	OPAL_XIVE_GET_IRQ_CONFIG	130
#define	OPAL_XIVE_SET_IRQ_CONFIG	131
#define	OPAL_XIVE_GET_QUEUE_INFO	132
#define	OPAL_XIVE_SET_QUEUE_INFO	133
#define	OPAL_XIVE_DONATE_PAGE		134
#define	OPAL_XIVE_ALLOCATE_VP_BLOCK	135
#define	OPAL_XIVE_FREE_VP_BLOCK		136
#define	OPAL_XIVE_GET_VP_INFO		137
#define	OPAL_XIVE_SET_VP_INFO		138
#define	OPAL_XIVE_ALLOCATE_IRQ		139
#define	OPAL_XIVE_FREE_IRQ		140
#define	OPAL_XIVE_SYNC			141
#define	OPAL_XIVE_DUMP			142
#define	OPAL_SENSOR_GROUP_CLEAR		156
#define	OPAL_SENSOR_READ_U64		162
#define	OPAL_SENSOR_GROUP_ENABLE	163
#define	OPAL_HANDLE_HMI2		166

/* For OPAL_PCI_SET_PE */
#define	OPAL_UNMAP_PE			0
#define OPAL_MAP_PE			1

#define	OPAL_PCI_BUS_ANY		0
#define	OPAL_PCI_BUS_3BITS		2
#define	OPAL_PCI_BUS_4BITS		3
#define	OPAL_PCI_BUS_5BITS		4
#define	OPAL_PCI_BUS_6BITS		5
#define	OPAL_PCI_BUS_7BITS		6
#define	OPAL_PCI_BUS_ALL		7 /* Match bus number exactly */

#define	OPAL_IGNORE_RID_DEVICE_NUMBER	0
#define	OPAL_COMPARE_RID_DEVICE_NUMBER	1

#define	OPAL_IGNORE_RID_FUNC_NUMBER	0
#define	OPAL_COMPARE_RID_FUNC_NUMBER	1

#define	OPAL_SUCCESS			0
#define	OPAL_PARAMETER			-1
#define	OPAL_BUSY			-2
#define	OPAL_CLOSED			-5
#define	OPAL_HARDWARE			-6
#define	OPAL_UNSUPPORTED		-7
#define	OPAL_RESOURCE			-10
#define	OPAL_BUSY_EVENT			-12
#define	OPAL_ASYNC_COMPLETION		-15
#define	OPAL_EMPTY			-16
#define	OPAL_XIVE_PROVISIONING		-31
#define	OPAL_XIVE_FREE_ACTIVE		-32

#define	OPAL_TOKEN_ABSENT		0
#define	OPAL_TOKEN_PRESENT		1

#define	OPAL_HMI_FLAGS_TB_RESYNC	(1ull << 0)
#define	OPAL_HMI_FLAGS_DEC_LOST		(1ull << 1)
#define	OPAL_HMI_FLAGS_HDEC_LOST	(1ull << 2)
#define	OPAL_HMI_FLAGS_TOD_TB_FAIL	(1ull << 3)
#define	OPAL_HMI_FLAGS_NEW_EVENT	(1ull << 63)

#define	OPAL_XIVE_XICS_MODE_EMU	0
#define	OPAL_XIVE_XICS_MODE_EXP	1

#define	OPAL_XIVE_VP_ENABLED		0x00000001
#define	OPAL_XIVE_VP_SINGLE_ESCALATION	0x00000002

#define	OPAL_XIVE_EQ_ENABLED		0x00000001
#define	OPAL_XIVE_EQ_ALWAYS_NOTIFY	0x00000002
#define	OPAL_XIVE_EQ_ESCALATE		0x00000004

struct opal_msg {
	uint32_t msg_type;
	uint32_t reserved;
	uint64_t params[8];
};

enum opal_msg_type {
	OPAL_MSG_ASYNC_COMP	= 0,
	OPAL_MSG_MEM_ERR	= 1,
	OPAL_MSG_EPOW		= 2,
	OPAL_MSG_SHUTDOWN	= 3,
	OPAL_MSG_HMI_EVT	= 4,
	OPAL_MSG_DPO		= 5,
	OPAL_MSG_PRD		= 6,
	OPAL_MSG_OCC		= 7,
	OPAL_MSG_TYPE_MAX,
};

#define	OPAL_IPMI_MSG_FORMAT_VERSION_1	1

struct opal_ipmi_msg {
	uint8_t version;
	uint8_t netfn;
	uint8_t cmd;
	uint8_t data[];
};

int	opal_init_async_tokens(int);
int	opal_alloc_async_token(void);
void	opal_free_async_token(int);
int	opal_wait_completion(void *, uint64_t, uint64_t);

#endif
