/* $OpenBSD: ipmivar.h,v 1.34 2021/01/23 12:10:08 kettenis Exp $ */

/*
 * Copyright (c) 2005 Jordan Hargrave
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _IPMIVAR_H_
#define _IPMIVAR_H_

#include <sys/rwlock.h>
#include <sys/sensors.h>
#include <sys/task.h>

#include <dev/ipmi.h>

#define IPMI_IF_KCS		1
#define IPMI_IF_SMIC		2
#define IPMI_IF_BT		3
#define IPMI_IF_SSIF		4

#define IPMI_IF_KCS_NREGS	2
#define IPMI_IF_SMIC_NREGS	3
#define IPMI_IF_BT_NREGS	3

struct ipmi_thread;
struct ipmi_softc;
struct ipmi_cmd;

struct ipmi_iowait {
	int			offset;
	u_int8_t		mask;
	u_int8_t		value;
	volatile u_int8_t	*v;
	const char		*lbl;
};

struct ipmi_attach_args {
	char		*iaa_name;
	bus_space_tag_t	iaa_iot;
	bus_space_tag_t	iaa_memt;

	int		iaa_if_type;
	int		iaa_if_rev;
	int		iaa_if_iotype;
	bus_addr_t	iaa_if_iobase;
	int		iaa_if_iosize;
	int		iaa_if_iospacing;
	int		iaa_if_irq;
	int		iaa_if_irqlvl;
};

struct ipmi_if {
	const char	*name;
	int		nregs;
	void		(*buildmsg)(struct ipmi_cmd *);
	int		(*sendmsg)(struct ipmi_cmd *);
	int		(*recvmsg)(struct ipmi_cmd *);
	int		(*reset)(struct ipmi_softc *);
	int		(*probe)(struct ipmi_softc *);
	int		datasnd;
	int		datarcv;
};

struct ipmi_cmd {
	struct ipmi_softc	*c_sc;

	int			c_rssa;
	int			c_rslun;
	int			c_netfn;
	int			c_cmd;

	int			c_txlen;
	int			c_maxrxlen;
	int			c_rxlen;

	void			*c_data;
	u_int			c_ccode;
};

struct ipmi_softc {
	struct device		sc_dev;

	struct ipmi_if		*sc_if;			/* Interface layer */
	int			sc_if_iosize;		/* Size of I/O porrs */
	int			sc_if_iospacing;	/* Spacing of I/O ports */
	int			sc_if_rev;		/* IPMI Revision */

	void			*sc_ih;			/* Interrupt/IO handles */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_btseq;
	u_int8_t		sc_buf[IPMI_MAX_RX + 16];
	struct taskq		*sc_cmd_taskq;

	struct ipmi_ioctl {
		struct rwlock		lock;
		struct ipmi_req		req;
		struct ipmi_cmd		cmd;
		uint8_t			buf[IPMI_MAX_RX];
	} sc_ioctl;

	int			sc_wdog_period;
	struct task		sc_wdog_tickle_task;

	struct ipmi_thread	*sc_thread;

	struct ipmi_sensor	*current_sensor;
	struct ksensordev	sc_sensordev;
};

struct ipmi_thread {
	struct ipmi_softc   *sc;
	volatile int	    running;
};

#define IPMI_WDOG_DONTSTOP	0x40

#define IPMI_WDOG_MASK		0x03
#define IPMI_WDOG_DISABLED	0x00
#define IPMI_WDOG_REBOOT	0x01
#define IPMI_WDOG_PWROFF	0x02
#define IPMI_WDOG_PWRCYCLE	0x03

#define IPMI_WDOG_PRE_DISABLED	0x00
#define IPMI_WDOG_PRE_SMI	0x01
#define IPMI_WDOG_PRE_NMI	0x02
#define IPMI_WDOG_PRE_INTERRUPT	0x03

#define	IPMI_SET_WDOG_TIMER	0
#define	IPMI_SET_WDOG_ACTION	1
#define	IPMI_SET_WDOG_PRETIMO	2
#define	IPMI_SET_WDOG_FLAGS	3
#define	IPMI_SET_WDOG_TIMOL	4
#define	IPMI_SET_WDOG_TIMOM	5
#define	IPMI_SET_WDOG_MAX	6

#define	IPMI_GET_WDOG_TIMER	IPMI_SET_WDOG_TIMER
#define	IPMI_GET_WDOG_ACTION	IPMI_SET_WDOG_ACTION
#define	IPMI_GET_WDOG_PRETIMO	IPMI_SET_WDOG_PRETIMO
#define	IPMI_GET_WDOG_FLAGS	IPMI_SET_WDOG_FLAGS
#define	IPMI_GET_WDOG_TIMOL	IPMI_SET_WDOG_TIMOL
#define	IPMI_GET_WDOG_TIMOM	IPMI_SET_WDOG_TIMOM
#define	IPMI_GET_WDOG_PRECDL	6
#define	IPMI_GET_WDOG_PRECDM	7
#define	IPMI_GET_WDOG_MAX	8

int	ipmi_probe(void *);
void	ipmi_attach_common(struct ipmi_softc *, struct ipmi_attach_args *);
int	ipmi_activate(struct device *, int);

int	ipmi_sendcmd(struct ipmi_cmd *);
int	ipmi_recvcmd(struct ipmi_cmd *);

#define IPMI_MSG_NFLN			0
#define IPMI_MSG_CMD			1
#define IPMI_MSG_CCODE			2
#define IPMI_MSG_DATASND		2
#define IPMI_MSG_DATARCV		3

#define APP_NETFN			0x06
#define APP_GET_DEVICE_ID		0x01
#define APP_RESET_WATCHDOG		0x22
#define APP_SET_WATCHDOG_TIMER		0x24
#define APP_GET_WATCHDOG_TIMER		0x25
#define APP_GET_SYSTEM_INTERFACE_CAPS	0x57

#define TRANSPORT_NETFN			0xC
#define BRIDGE_NETFN			0x2

#define STORAGE_NETFN			0x0A
#define STORAGE_GET_FRU_INV_AREA	0x10
#define STORAGE_READ_FRU_DATA		0x11
#define STORAGE_RESERVE_SDR		0x22
#define STORAGE_GET_SDR			0x23
#define STORAGE_ADD_SDR			0x24
#define STORAGE_ADD_PARTIAL_SDR		0x25
#define STORAGE_DELETE_SDR		0x26
#define STORAGE_RESERVE_SEL		0x42
#define STORAGE_GET_SEL			0x43
#define STORAGE_ADD_SEL			0x44
#define STORAGE_ADD_PARTIAL_SEL		0x45
#define STORAGE_DELETE_SEL		0x46

#define SE_NETFN			0x04
#define SE_GET_SDR_INFO			0x20
#define SE_GET_SDR			0x21
#define SE_RESERVE_SDR			0x22
#define SE_GET_SENSOR_FACTOR		0x23
#define SE_SET_SENSOR_HYSTERESIS	0x24
#define SE_GET_SENSOR_HYSTERESIS	0x25
#define SE_SET_SENSOR_THRESHOLD		0x26
#define SE_GET_SENSOR_THRESHOLD		0x27
#define SE_SET_SENSOR_EVENT_ENABLE	0x28
#define SE_GET_SENSOR_EVENT_ENABLE	0x29
#define SE_REARM_SENSOR_EVENTS		0x2A
#define SE_GET_SENSOR_EVENT_STATUS	0x2B
#define SE_GET_SENSOR_READING		0x2D
#define SE_SET_SENSOR_TYPE		0x2E
#define SE_GET_SENSOR_TYPE		0x2F

struct sdrhdr {
	u_int16_t	record_id;		/* SDR Record ID */
	u_int8_t	sdr_version;		/* SDR Version */
	u_int8_t	record_type;		/* SDR Record Type */
	u_int8_t	record_length;		/* SDR Record Length */
} __packed;

/* SDR: Record Type 1 */
struct sdrtype1 {
	struct sdrhdr	sdrhdr;

	u_int8_t	owner_id;
	u_int8_t	owner_lun;
	u_int8_t	sensor_num;

	u_int8_t	entity_id;
	u_int8_t	entity_instance;
	u_int8_t	sensor_init;
	u_int8_t	sensor_caps;
	u_int8_t	sensor_type;
	u_int8_t	event_code;
	u_int16_t	trigger_mask;
	u_int16_t	reading_mask;
	u_int16_t	settable_mask;
	u_int8_t	units1;
	u_int8_t	units2;
	u_int8_t	units3;
	u_int8_t	linear;
	u_int8_t	m;
	u_int8_t	m_tolerance;
	u_int8_t	b;
	u_int8_t	b_accuracy;
	u_int8_t	accuracyexp;
	u_int8_t	rbexp;
	u_int8_t	analogchars;
	u_int8_t	nominalreading;
	u_int8_t	normalmax;
	u_int8_t	normalmin;
	u_int8_t	sensormax;
	u_int8_t	sensormin;
	u_int8_t	uppernr;
	u_int8_t	upperc;
	u_int8_t	uppernc;
	u_int8_t	lowernr;
	u_int8_t	lowerc;
	u_int8_t	lowernc;
	u_int8_t	physt;
	u_int8_t	nhyst;
	u_int8_t	resvd[2];
	u_int8_t	oem;
	u_int8_t	typelen;
	u_int8_t	name[1];
} __packed;

/* SDR: Record Type 2 */
struct sdrtype2 {
	struct sdrhdr	sdrhdr;

	u_int8_t	owner_id;
	u_int8_t	owner_lun;
	u_int8_t	sensor_num;

	u_int8_t	entity_id;
	u_int8_t	entity_instance;
	u_int8_t	sensor_init;
	u_int8_t	sensor_caps;
	u_int8_t	sensor_type;
	u_int8_t	event_code;
	u_int16_t	trigger_mask;
	u_int16_t	reading_mask;
	u_int16_t	set_mask;
	u_int8_t	units1;
	u_int8_t	units2;
	u_int8_t	units3;
	u_int8_t	share1;
	u_int8_t	share2;
	u_int8_t	physt;
	u_int8_t	nhyst;
	u_int8_t	resvd[3];
	u_int8_t	oem;
	u_int8_t	typelen;
	u_int8_t	name[1];
} __packed;

#endif				/* _IPMIVAR_H_ */
