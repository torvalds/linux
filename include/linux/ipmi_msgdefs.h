/*
 * ipmi_smi.h
 *
 * MontaVista IPMI system management interface
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_IPMI_MSGDEFS_H
#define __LINUX_IPMI_MSGDEFS_H

/* Various definitions for IPMI messages used by almost everything in
   the IPMI stack. */

/* NetFNs and commands used inside the IPMI stack. */

#define IPMI_NETFN_SENSOR_EVENT_REQUEST		0x04
#define IPMI_NETFN_SENSOR_EVENT_RESPONSE	0x05
#define IPMI_GET_EVENT_RECEIVER_CMD	0x01

#define IPMI_NETFN_APP_REQUEST			0x06
#define IPMI_NETFN_APP_RESPONSE			0x07
#define IPMI_GET_DEVICE_ID_CMD		0x01
#define IPMI_COLD_RESET_CMD		0x02
#define IPMI_WARM_RESET_CMD		0x03
#define IPMI_CLEAR_MSG_FLAGS_CMD	0x30
#define IPMI_GET_DEVICE_GUID_CMD	0x08
#define IPMI_GET_MSG_FLAGS_CMD		0x31
#define IPMI_SEND_MSG_CMD		0x34
#define IPMI_GET_MSG_CMD		0x33
#define IPMI_SET_BMC_GLOBAL_ENABLES_CMD	0x2e
#define IPMI_GET_BMC_GLOBAL_ENABLES_CMD	0x2f
#define IPMI_READ_EVENT_MSG_BUFFER_CMD	0x35
#define IPMI_GET_CHANNEL_INFO_CMD	0x42

#define IPMI_NETFN_STORAGE_REQUEST		0x0a
#define IPMI_NETFN_STORAGE_RESPONSE		0x0b
#define IPMI_ADD_SEL_ENTRY_CMD		0x44

#define IPMI_NETFN_FIRMWARE_REQUEST		0x08
#define IPMI_NETFN_FIRMWARE_RESPONSE		0x09

/* The default slave address */
#define IPMI_BMC_SLAVE_ADDR	0x20

/* The BT interface on high-end HP systems supports up to 255 bytes in
 * one transfer.  Its "virtual" BMC supports some commands that are longer
 * than 128 bytes.  Use the full 256, plus NetFn/LUN, Cmd, cCode, plus
 * some overhead.  It would be nice to base this on the "BT Capabilities"
 * but that's too hard to propagate to the rest of the driver. */
#define IPMI_MAX_MSG_LENGTH	272	/* multiple of 16 */

#define IPMI_CC_NO_ERROR		0x00
#define IPMI_NODE_BUSY_ERR		0xc0
#define IPMI_INVALID_COMMAND_ERR	0xc1
#define IPMI_ERR_MSG_TRUNCATED		0xc6
#define IPMI_LOST_ARBITRATION_ERR	0x81
#define IPMI_BUS_ERR			0x82
#define IPMI_NAK_ON_WRITE_ERR		0x83
#define IPMI_ERR_UNSPECIFIED		0xff

#define IPMI_CHANNEL_PROTOCOL_IPMB	1
#define IPMI_CHANNEL_PROTOCOL_ICMB	2
#define IPMI_CHANNEL_PROTOCOL_SMBUS	4
#define IPMI_CHANNEL_PROTOCOL_KCS	5
#define IPMI_CHANNEL_PROTOCOL_SMIC	6
#define IPMI_CHANNEL_PROTOCOL_BT10	7
#define IPMI_CHANNEL_PROTOCOL_BT15	8
#define IPMI_CHANNEL_PROTOCOL_TMODE	9

#define IPMI_CHANNEL_MEDIUM_IPMB	1
#define IPMI_CHANNEL_MEDIUM_ICMB10	2
#define IPMI_CHANNEL_MEDIUM_ICMB09	3
#define IPMI_CHANNEL_MEDIUM_8023LAN	4
#define IPMI_CHANNEL_MEDIUM_ASYNC	5
#define IPMI_CHANNEL_MEDIUM_OTHER_LAN	6
#define IPMI_CHANNEL_MEDIUM_PCI_SMBUS	7
#define IPMI_CHANNEL_MEDIUM_SMBUS1	8
#define IPMI_CHANNEL_MEDIUM_SMBUS2	9
#define IPMI_CHANNEL_MEDIUM_USB1	10
#define IPMI_CHANNEL_MEDIUM_USB2	11
#define IPMI_CHANNEL_MEDIUM_SYSINTF	12

#endif /* __LINUX_IPMI_MSGDEFS_H */
