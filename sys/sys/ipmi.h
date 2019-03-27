/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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

#ifndef __SYS_IPMI_H__
#define	__SYS_IPMI_H__

#define IPMI_MAX_ADDR_SIZE		0x20
#define IPMI_MAX_RX			1024
#define IPMI_BMC_SLAVE_ADDR		0x20 /* Linux Default slave address */
#define IPMI_BMC_CHANNEL		0x0f /* Linux BMC channel */

#define IPMI_BMC_SMS_LUN		0x02

#define IPMI_SYSTEM_INTERFACE_ADDR_TYPE	0x0c
#define IPMI_IPMB_ADDR_TYPE		0x01
#define IPMI_IPMB_BROADCAST_ADDR_TYPE	0x41

#define IPMI_IOC_MAGIC			'i'
#define IPMICTL_RECEIVE_MSG_TRUNC	_IOWR(IPMI_IOC_MAGIC, 11, struct ipmi_recv)
#define IPMICTL_RECEIVE_MSG		_IOWR(IPMI_IOC_MAGIC, 12, struct ipmi_recv)
#define IPMICTL_SEND_COMMAND		_IOW(IPMI_IOC_MAGIC, 13, struct ipmi_req)
#define IPMICTL_REGISTER_FOR_CMD	_IOW(IPMI_IOC_MAGIC, 14, struct ipmi_cmdspec)
#define IPMICTL_UNREGISTER_FOR_CMD	_IOW(IPMI_IOC_MAGIC, 15, struct ipmi_cmdspec)
#define IPMICTL_SET_GETS_EVENTS_CMD	_IOW(IPMI_IOC_MAGIC, 16, int)
#define IPMICTL_SET_MY_ADDRESS_CMD	_IOW(IPMI_IOC_MAGIC, 17, unsigned int)
#define IPMICTL_GET_MY_ADDRESS_CMD	_IOR(IPMI_IOC_MAGIC, 18, unsigned int)
#define IPMICTL_SET_MY_LUN_CMD		_IOW(IPMI_IOC_MAGIC, 19, unsigned int)
#define IPMICTL_GET_MY_LUN_CMD		_IOR(IPMI_IOC_MAGIC, 20, unsigned int)

#define IPMI_RESPONSE_RECV_TYPE         1
#define IPMI_ASYNC_EVENT_RECV_TYPE      2
#define IPMI_CMD_RECV_TYPE              3

#define	IPMI_CHASSIS_REQUEST		0x00
# define IPMI_CHASSIS_CONTROL		0x02
#  define IPMI_CC_POWER_DOWN		0x0
#  define IPMI_CC_POWER_UP		0x1
#  define IPMI_CC_POWER_CYCLE		0x2
#  define IPMI_CC_HARD_RESET		0x3
#  define IPMI_CC_PULSE_DI		0x4
#  define IPMI_CC_SOFT_OVERTEMP		0x5

#define IPMI_APP_REQUEST		0x06
#define IPMI_GET_DEVICE_ID		0x01
# define IPMI_ADS_CHASSIS		0x80
# define IPMI_ADS_BRIDGE		0x40
# define IPMI_ADS_EVENT_GEN		0x20
# define IPMI_ADS_EVENT_RCV		0x10
# define IPMI_ADS_FRU_INV		0x08
# define IPMI_ADS_SEL			0x04
# define IPMI_ADS_SDR			0x02
# define IPMI_ADS_SENSOR		0x01
#define IPMI_CLEAR_FLAGS		0x30
#define IPMI_GET_MSG_FLAGS		0x31
# define IPMI_MSG_AVAILABLE		0x01
# define IPMI_MSG_BUFFER_FULL		0x02
# define IPMI_WDT_PRE_TIMEOUT		0x08
#define IPMI_GET_MSG			0x33
#define IPMI_SEND_MSG			0x34
#define IPMI_GET_CHANNEL_INFO		0x42
#define IPMI_RESET_WDOG			0x22
#define IPMI_SET_WDOG			0x24
#define IPMI_GET_WDOG			0x25

#define IPMI_SET_WD_TIMER_SMS_OS	0x04
#define IPMI_SET_WD_TIMER_DONT_STOP	0x40
#define IPMI_SET_WD_ACTION_NONE		0x00
#define IPMI_SET_WD_ACTION_RESET	0x01
#define IPMI_SET_WD_ACTION_POWER_DOWN	0x02
#define IPMI_SET_WD_ACTION_POWER_CYCLE	0x03
#define IPMI_SET_WD_PREACTION_NONE	(0x00 << 4)
#define IPMI_SET_WD_PREACTION_SMI	(0x01 << 4)
#define IPMI_SET_WD_PREACTION_NMI	(0x02 << 4)
#define IPMI_SET_WD_PREACTION_MI	(0x03 << 4)

struct ipmi_msg {
	unsigned char	netfn;
        unsigned char	cmd;
        unsigned short	data_len;
        unsigned char	*data;
};

struct ipmi_req {
	unsigned char	*addr;
	unsigned int	addr_len;
	long		msgid;
	struct ipmi_msg	msg;
};

struct ipmi_recv {
	int		recv_type;
	unsigned char	*addr;
	unsigned int	addr_len;
	long		msgid;
	struct ipmi_msg	msg;
};

struct ipmi_cmdspec {
	unsigned char	netfn;
	unsigned char	cmd;
};


struct ipmi_addr {
	int		addr_type;
	short		channel;
	unsigned char	data[IPMI_MAX_ADDR_SIZE];
};

struct ipmi_system_interface_addr {
	int		addr_type;
	short		channel;
	unsigned char	lun;
};

struct ipmi_ipmb_addr {
	int		addr_type;
	short		channel;
	unsigned char	slave_addr;
	unsigned char	lun;
};

#if defined(__amd64__)
/* Compatibility with 32-bit binaries. */

#define IPMICTL_RECEIVE_MSG_TRUNC_32	_IOWR(IPMI_IOC_MAGIC, 11, struct ipmi_recv32)
#define IPMICTL_RECEIVE_MSG_32		_IOWR(IPMI_IOC_MAGIC, 12, struct ipmi_recv32)
#define IPMICTL_SEND_COMMAND_32		_IOW(IPMI_IOC_MAGIC, 13, struct ipmi_req32)

struct ipmi_msg32 {
	unsigned char	netfn;
        unsigned char	cmd;
        unsigned short	data_len;
	uint32_t	data;
};

struct ipmi_req32 {
	uint32_t	addr;
	unsigned int	addr_len;
	int32_t		msgid;
	struct ipmi_msg32 msg;
};

struct ipmi_recv32 {
	int		recv_type;
	uint32_t	addr;
	unsigned int	addr_len;
	int32_t		msgid;
	struct ipmi_msg32 msg;
};

#endif

#endif	/* !__SYS_IPMI_H__ */
