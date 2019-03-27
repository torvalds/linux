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

#ifndef __IPMIVARS_H__
#define	__IPMIVARS_H__

struct ipmi_get_info {
	int		iface_type;
	uint64_t	address;
	int		offset;
	int		io_mode;
	int		irq;
};

struct ipmi_device;

struct ipmi_request {
	TAILQ_ENTRY(ipmi_request) ir_link;
	struct ipmi_device *ir_owner;	/* Driver uses NULL. */
	u_char		*ir_request;	/* Request is data to send to BMC. */
	size_t		ir_requestlen;
	u_char		*ir_reply;	/* Reply is data read from BMC. */
	size_t		ir_replybuflen;	/* Length of ir_reply[] buffer. */
	int		ir_replylen;	/* Length of reply from BMC. */
	int		ir_error;
	long		ir_msgid;
	uint8_t		ir_addr;
	uint8_t		ir_command;
	uint8_t		ir_compcode;
};

#define	MAX_RES				3
#define KCS_DATA			0
#define KCS_CTL_STS			1
#define SMIC_DATA			0
#define SMIC_CTL_STS			1
#define SMIC_FLAGS			2

struct ipmi_softc;

/* Per file descriptor data. */
struct ipmi_device {
	TAILQ_ENTRY(ipmi_device) ipmi_link;
	TAILQ_HEAD(,ipmi_request) ipmi_completed_requests;
	struct selinfo		ipmi_select;
	struct ipmi_softc	*ipmi_softc;
	int			ipmi_closing;
	int			ipmi_requests;
	u_char			ipmi_address;	/* IPMB address. */
	u_char			ipmi_lun;
};

struct ipmi_kcs {
};

struct ipmi_smic {
};

struct ipmi_ssif {
	device_t smbus;
	int	smbus_address;
};

struct ipmi_softc {
	device_t		ipmi_dev;
	union {
		struct ipmi_kcs kcs;
		struct ipmi_smic smic;
		struct ipmi_ssif ssif;
	} _iface;
	int			ipmi_io_rid;
	int			ipmi_io_type;
	struct mtx		ipmi_io_lock;
	struct resource		*ipmi_io_res[MAX_RES];
	int			ipmi_io_spacing;
	int			ipmi_irq_rid;
	struct resource		*ipmi_irq_res;
	void			*ipmi_irq;
	int			ipmi_detaching;
	int			ipmi_opened;
	uint8_t			ipmi_dev_support;	/* IPMI_ADS_* */
	struct cdev		*ipmi_cdev;
	TAILQ_HEAD(,ipmi_request) ipmi_pending_requests;
	int			ipmi_driver_requests_polled;
	eventhandler_tag	ipmi_power_cycle_tag;
	eventhandler_tag	ipmi_watchdog_tag;
	eventhandler_tag	ipmi_shutdown_tag;
	int			ipmi_watchdog_active;
	int			ipmi_watchdog_actions;
	int			ipmi_watchdog_pretimeout;
	struct intr_config_hook	ipmi_ich;
	struct mtx		ipmi_requests_lock;
	struct cv		ipmi_request_added;
	struct proc		*ipmi_kthread;
	driver_intr_t		*ipmi_intr;
	int			(*ipmi_startup)(struct ipmi_softc *);
	int			(*ipmi_enqueue_request)(struct ipmi_softc *, struct ipmi_request *);
	int			(*ipmi_driver_request)(struct ipmi_softc *, struct ipmi_request *, int);
};

#define	ipmi_ssif_smbus_address		_iface.ssif.smbus_address
#define	ipmi_ssif_smbus			_iface.ssif.smbus

struct ipmi_ipmb {
	u_char foo;
};

#define KCS_MODE		0x01
#define SMIC_MODE		0x02
#define	BT_MODE			0x03
#define SSIF_MODE		0x04

/* KCS status flags */
#define KCS_STATUS_OBF			0x01 /* Data Out ready from BMC */
#define KCS_STATUS_IBF			0x02 /* Data In from System */
#define KCS_STATUS_SMS_ATN		0x04 /* Ready in RX queue */
#define KCS_STATUS_C_D			0x08 /* Command/Data register write*/
#define KCS_STATUS_OEM1			0x10
#define KCS_STATUS_OEM2			0x20
#define KCS_STATUS_S0			0x40
#define KCS_STATUS_S1			0x80
 #define KCS_STATUS_STATE(x)		((x)>>6)
 #define KCS_STATUS_STATE_IDLE		0x0
 #define KCS_STATUS_STATE_READ		0x1
 #define KCS_STATUS_STATE_WRITE		0x2
 #define KCS_STATUS_STATE_ERROR		0x3
#define	KCS_IFACE_STATUS_OK		0x00
#define KCS_IFACE_STATUS_ABORT		0x01
#define KCS_IFACE_STATUS_ILLEGAL	0x02
#define KCS_IFACE_STATUS_LENGTH_ERR	0x06
#define	KCS_IFACE_STATUS_UNKNOWN_ERR	0xff

/* KCS control codes */
#define KCS_CONTROL_GET_STATUS_ABORT	0x60
#define KCS_CONTROL_WRITE_START		0x61
#define KCS_CONTROL_WRITE_END		0x62
#define KCS_DATA_IN_READ		0x68

/* SMIC status flags */
#define SMIC_STATUS_BUSY		0x01 /* System set and BMC clears it */
#define SMIC_STATUS_SMS_ATN		0x04 /* BMC has a message */
#define SMIC_STATUS_EVT_ATN		0x08 /* Event has been RX */
#define SMIC_STATUS_SMI			0x10 /* asserted SMI */
#define SMIC_STATUS_TX_RDY		0x40 /* Ready to accept WRITE */
#define SMIC_STATUS_RX_RDY		0x80 /* Ready to read */
#define	SMIC_STATUS_RESERVED		0x22

/* SMIC control codes */
#define SMIC_CC_SMS_GET_STATUS		0x40
#define SMIC_CC_SMS_WR_START		0x41
#define SMIC_CC_SMS_WR_NEXT		0x42
#define SMIC_CC_SMS_WR_END		0x43
#define SMIC_CC_SMS_RD_START		0x44
#define SMIC_CC_SMS_RD_NEXT		0x45
#define SMIC_CC_SMS_RD_END		0x46

/* SMIC status codes */
#define SMIC_SC_SMS_RDY			0xc0
#define SMIC_SC_SMS_WR_START		0xc1
#define SMIC_SC_SMS_WR_NEXT		0xc2
#define SMIC_SC_SMS_WR_END		0xc3
#define SMIC_SC_SMS_RD_START		0xc4
#define SMIC_SC_SMS_RD_NEXT		0xc5
#define SMIC_SC_SMS_RD_END		0xc6

#define	IPMI_ADDR(netfn, lun)		((netfn) << 2 | (lun))
#define	IPMI_REPLY_ADDR(addr)		((addr) + 0x4)

#define	IPMI_LOCK(sc)		mtx_lock(&(sc)->ipmi_requests_lock)
#define	IPMI_UNLOCK(sc)		mtx_unlock(&(sc)->ipmi_requests_lock)
#define	IPMI_LOCK_ASSERT(sc)	mtx_assert(&(sc)->ipmi_requests_lock, MA_OWNED)

#define	IPMI_IO_LOCK(sc)	mtx_lock(&(sc)->ipmi_io_lock)
#define	IPMI_IO_UNLOCK(sc)	mtx_unlock(&(sc)->ipmi_io_lock)
#define	IPMI_IO_LOCK_ASSERT(sc)	mtx_assert(&(sc)->ipmi_io_lock, MA_OWNED)

/* I/O to a single I/O resource. */
#define INB_SINGLE(sc, x)						\
	bus_read_1((sc)->ipmi_io_res[0], (sc)->ipmi_io_spacing * (x))
#define OUTB_SINGLE(sc, x, value)					\
	bus_write_1((sc)->ipmi_io_res[0], (sc)->ipmi_io_spacing * (x), value)

/* I/O with each register in its in I/O resource. */
#define INB_MULTIPLE(sc, x)			\
	bus_read_1((sc)->ipmi_io_res[(x)], 0)
#define OUTB_MULTIPLE(sc, x, value)					\
	bus_write_1((sc)->ipmi_io_res[(x)], 0, value)

/*
 * Determine I/O method based on whether or not we have more than one I/O
 * resource.
 */
#define	INB(sc, x)							\
	((sc)->ipmi_io_res[1] != NULL ? INB_MULTIPLE(sc, x) : INB_SINGLE(sc, x))
#define	OUTB(sc, x, value)						\
	((sc)->ipmi_io_res[1] != NULL ? OUTB_MULTIPLE(sc, x, value) :	\
	    OUTB_SINGLE(sc, x, value))

#define MAX_TIMEOUT 6 * hz

int	ipmi_attach(device_t);
int	ipmi_detach(device_t);
void	ipmi_release_resources(device_t);

/* Manage requests. */
struct ipmi_request *ipmi_alloc_request(struct ipmi_device *, long, uint8_t,
	    uint8_t, size_t, size_t);
void	ipmi_complete_request(struct ipmi_softc *, struct ipmi_request *);
struct ipmi_request *ipmi_dequeue_request(struct ipmi_softc *);
void	ipmi_free_request(struct ipmi_request *);
int	ipmi_polled_enqueue_request(struct ipmi_softc *, struct ipmi_request *);
int	ipmi_submit_driver_request(struct ipmi_softc *, struct ipmi_request *,
	    int);

/* Identify BMC interface via SMBIOS. */
int	ipmi_smbios_identify(struct ipmi_get_info *);

/* Match BMC PCI device listed in SMBIOS. */
const char *ipmi_pci_match(uint16_t, uint16_t);

/* Interface attach routines. */
int	ipmi_kcs_attach(struct ipmi_softc *);
int	ipmi_kcs_probe_align(struct ipmi_softc *);
int	ipmi_smic_attach(struct ipmi_softc *);
int	ipmi_ssif_attach(struct ipmi_softc *, device_t, int);

#ifdef IPMB
int	ipmi_handle_attn(struct ipmi_softc *);
#endif

extern devclass_t ipmi_devclass;
extern int ipmi_attached;

#endif	/* !__IPMIVARS_H__ */
