/*	$OpenBSD: ipmi.c,v 1.119 2024/04/03 18:32:47 gkoehler Exp $ */

/*
 * Copyright (c) 2015 Masao Uebayashi
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/extent.h>
#include <sys/sensors.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/smbiosvar.h>

#include <dev/ipmivar.h>
#include <dev/ipmi.h>

struct ipmi_sensor {
	u_int8_t	*i_sdr;
	int		i_num;
	int		stype;
	int		etype;
	struct		ksensor i_sensor;
	SLIST_ENTRY(ipmi_sensor) list;
};

int	ipmi_enabled = 0;

#define SENSOR_REFRESH_RATE 5	/* seconds */

#define DEVNAME(s)  ((s)->sc_dev.dv_xname)

#define IPMI_BTMSG_LEN			0
#define IPMI_BTMSG_NFLN			1
#define IPMI_BTMSG_SEQ			2
#define IPMI_BTMSG_CMD			3
#define IPMI_BTMSG_CCODE		4
#define IPMI_BTMSG_DATASND		4
#define IPMI_BTMSG_DATARCV		5

/* IPMI 2.0, Table 42-3: Sensor Type Codes */
#define IPMI_SENSOR_TYPE_TEMP		0x0101
#define IPMI_SENSOR_TYPE_VOLT		0x0102
#define IPMI_SENSOR_TYPE_CURRENT	0x0103
#define IPMI_SENSOR_TYPE_FAN		0x0104
#define IPMI_SENSOR_TYPE_INTRUSION	0x6F05
#define IPMI_SENSOR_TYPE_PWRSUPPLY	0x6F08

/* IPMI 2.0, Table 43-15: Sensor Unit Type Codes */
#define IPMI_UNIT_TYPE_DEGREE_C		1
#define IPMI_UNIT_TYPE_DEGREE_F		2
#define IPMI_UNIT_TYPE_DEGREE_K		3
#define IPMI_UNIT_TYPE_VOLTS		4
#define IPMI_UNIT_TYPE_AMPS		5
#define IPMI_UNIT_TYPE_WATTS		6
#define IPMI_UNIT_TYPE_RPM		18

#define IPMI_NAME_UNICODE		0x00
#define IPMI_NAME_BCDPLUS		0x01
#define IPMI_NAME_ASCII6BIT		0x02
#define IPMI_NAME_ASCII8BIT		0x03

#define IPMI_ENTITY_PWRSUPPLY		0x0A

#define IPMI_INVALID_SENSOR		(1L << 5)
#define IPMI_DISABLED_SENSOR		(1L << 6)

#define IPMI_SDR_TYPEFULL		1
#define IPMI_SDR_TYPECOMPACT		2

#define byteof(x) ((x) >> 3)
#define bitof(x)  (1L << ((x) & 0x7))
#define TB(b,m)	  (data[2+byteof(b)] & bitof(b))

#ifdef IPMI_DEBUG
int	ipmi_dbg = 0;
#define dbg_printf(lvl, fmt...) \
	if (ipmi_dbg >= lvl) \
		printf(fmt);
#define dbg_dump(lvl, msg, len, buf) \
	if (len && ipmi_dbg >= lvl) \
		dumpb(msg, len, (const u_int8_t *)(buf));
#else
#define dbg_printf(lvl, fmt...)
#define dbg_dump(lvl, msg, len, buf)
#endif

long signextend(unsigned long, int);

SLIST_HEAD(ipmi_sensors_head, ipmi_sensor);
struct ipmi_sensors_head ipmi_sensor_list =
    SLIST_HEAD_INITIALIZER(ipmi_sensor_list);

void	dumpb(const char *, int, const u_int8_t *);

int	read_sensor(struct ipmi_softc *, struct ipmi_sensor *);
int	add_sdr_sensor(struct ipmi_softc *, u_int8_t *, int);
int	get_sdr_partial(struct ipmi_softc *, u_int16_t, u_int16_t,
	    u_int8_t, u_int8_t, void *, u_int16_t *);
int	get_sdr(struct ipmi_softc *, u_int16_t, u_int16_t *);

int	ipmi_sendcmd(struct ipmi_cmd *);
int	ipmi_recvcmd(struct ipmi_cmd *);
void	ipmi_cmd(struct ipmi_cmd *);
void	ipmi_cmd_poll(struct ipmi_cmd *);
void	ipmi_cmd_wait(struct ipmi_cmd *);
void	ipmi_cmd_wait_cb(void *);

int	ipmi_watchdog(void *, int);
void	ipmi_watchdog_tickle(void *);
void	ipmi_watchdog_set(void *);

struct ipmi_softc *ipmilookup(dev_t dev);

int	ipmiopen(dev_t, int, int, struct proc *);
int	ipmiclose(dev_t, int, int, struct proc *);
int	ipmiioctl(dev_t, u_long, caddr_t, int, struct proc *);

long	ipow(long, int);
long	ipmi_convert(u_int8_t, struct sdrtype1 *, long);
int	ipmi_sensor_name(char *, int, u_int8_t, u_int8_t *, int);

/* BMC Helper Functions */
u_int8_t bmc_read(struct ipmi_softc *, int);
void	bmc_write(struct ipmi_softc *, int, u_int8_t);
int	bmc_io_wait(struct ipmi_softc *, struct ipmi_iowait *);

void	bt_buildmsg(struct ipmi_cmd *);
void	cmn_buildmsg(struct ipmi_cmd *);

int	getbits(u_int8_t *, int, int);
int	ipmi_sensor_type(int, int, int, int);

void	ipmi_refresh_sensors(struct ipmi_softc *sc);
int	ipmi_map_regs(struct ipmi_softc *sc, struct ipmi_attach_args *ia);
void	ipmi_unmap_regs(struct ipmi_softc *);

int	ipmi_sensor_status(struct ipmi_softc *, struct ipmi_sensor *,
    u_int8_t *);

int	 add_child_sensors(struct ipmi_softc *, u_int8_t *, int, int, int,
    int, int, int, const char *);

void	ipmi_create_thread(void *);
void	ipmi_poll_thread(void *);

int	kcs_probe(struct ipmi_softc *);
int	kcs_reset(struct ipmi_softc *);
int	kcs_sendmsg(struct ipmi_cmd *);
int	kcs_recvmsg(struct ipmi_cmd *);

int	bt_probe(struct ipmi_softc *);
int	bt_reset(struct ipmi_softc *);
int	bt_sendmsg(struct ipmi_cmd *);
int	bt_recvmsg(struct ipmi_cmd *);

int	smic_probe(struct ipmi_softc *);
int	smic_reset(struct ipmi_softc *);
int	smic_sendmsg(struct ipmi_cmd *);
int	smic_recvmsg(struct ipmi_cmd *);

struct ipmi_if kcs_if = {
	"KCS",
	IPMI_IF_KCS_NREGS,
	cmn_buildmsg,
	kcs_sendmsg,
	kcs_recvmsg,
	kcs_reset,
	kcs_probe,
	IPMI_MSG_DATASND,
	IPMI_MSG_DATARCV,
};

struct ipmi_if smic_if = {
	"SMIC",
	IPMI_IF_SMIC_NREGS,
	cmn_buildmsg,
	smic_sendmsg,
	smic_recvmsg,
	smic_reset,
	smic_probe,
	IPMI_MSG_DATASND,
	IPMI_MSG_DATARCV,
};

struct ipmi_if bt_if = {
	"BT",
	IPMI_IF_BT_NREGS,
	bt_buildmsg,
	bt_sendmsg,
	bt_recvmsg,
	bt_reset,
	bt_probe,
	IPMI_BTMSG_DATASND,
	IPMI_BTMSG_DATARCV,
};

struct ipmi_if *ipmi_get_if(int);

struct ipmi_if *
ipmi_get_if(int iftype)
{
	switch (iftype) {
	case IPMI_IF_KCS:
		return (&kcs_if);
	case IPMI_IF_SMIC:
		return (&smic_if);
	case IPMI_IF_BT:
		return (&bt_if);
	}

	return (NULL);
}

/*
 * BMC Helper Functions
 */
u_int8_t
bmc_read(struct ipmi_softc *sc, int offset)
{
	if (sc->sc_if_iosize == 4)
		return (bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    offset * sc->sc_if_iospacing));
	else	
		return (bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    offset * sc->sc_if_iospacing));
}

void
bmc_write(struct ipmi_softc *sc, int offset, u_int8_t val)
{
	if (sc->sc_if_iosize == 4)
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    offset * sc->sc_if_iospacing, val);
	else
		bus_space_write_1(sc->sc_iot, sc->sc_ioh,
		    offset * sc->sc_if_iospacing, val);
}

int
bmc_io_wait(struct ipmi_softc *sc, struct ipmi_iowait *a)
{
	volatile u_int8_t	v;
	int			count = 5000000; /* == 5s XXX can be shorter */

	while (count--) {
		v = bmc_read(sc, a->offset);
		if ((v & a->mask) == a->value)
			return v;

		delay(1);
	}

	dbg_printf(1, "%s: bmc_io_wait fails : *v=%.2x m=%.2x b=%.2x %s\n",
	    DEVNAME(sc), v, a->mask, a->value, a->lbl);
	return (-1);

}

#define RSSA_MASK 0xff
#define LUN_MASK 0x3
#define NETFN_LUN(nf,ln) (((nf) << 2) | ((ln) & LUN_MASK))

/*
 * BT interface
 */
#define _BT_CTRL_REG			0
#define	  BT_CLR_WR_PTR			(1L << 0)
#define	  BT_CLR_RD_PTR			(1L << 1)
#define	  BT_HOST2BMC_ATN		(1L << 2)
#define	  BT_BMC2HOST_ATN		(1L << 3)
#define	  BT_EVT_ATN			(1L << 4)
#define	  BT_HOST_BUSY			(1L << 6)
#define	  BT_BMC_BUSY			(1L << 7)

#define	  BT_READY	(BT_HOST_BUSY|BT_HOST2BMC_ATN|BT_BMC2HOST_ATN)

#define _BT_DATAIN_REG			1
#define _BT_DATAOUT_REG			1

#define _BT_INTMASK_REG			2
#define	 BT_IM_HIRQ_PEND		(1L << 1)
#define	 BT_IM_SCI_EN			(1L << 2)
#define	 BT_IM_SMI_EN			(1L << 3)
#define	 BT_IM_NMI2SMI			(1L << 4)

int bt_read(struct ipmi_softc *, int);
int bt_write(struct ipmi_softc *, int, uint8_t);

int
bt_read(struct ipmi_softc *sc, int reg)
{
	return bmc_read(sc, reg);
}

int
bt_write(struct ipmi_softc *sc, int reg, uint8_t data)
{
	struct ipmi_iowait a;

	a.offset = _BT_CTRL_REG;
	a.mask = BT_BMC_BUSY;
	a.value = 0;
	a.lbl = "bt_write";
	if (bmc_io_wait(sc, &a) < 0)
		return (-1);

	bmc_write(sc, reg, data);
	return (0);
}

int
bt_sendmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	struct ipmi_iowait a;
	int i;

	bt_write(sc, _BT_CTRL_REG, BT_CLR_WR_PTR);
	for (i = 0; i < c->c_txlen; i++)
		bt_write(sc, _BT_DATAOUT_REG, sc->sc_buf[i]);

	bt_write(sc, _BT_CTRL_REG, BT_HOST2BMC_ATN);
	a.offset = _BT_CTRL_REG;
	a.mask = BT_HOST2BMC_ATN | BT_BMC_BUSY;
	a.value = 0;
	a.lbl = "bt_sendwait";
	if (bmc_io_wait(sc, &a) < 0)
		return (-1);

	return (0);
}

int
bt_recvmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	struct ipmi_iowait a;
	u_int8_t len, v, i, j;

	a.offset = _BT_CTRL_REG;
	a.mask = BT_BMC2HOST_ATN;
	a.value = BT_BMC2HOST_ATN;
	a.lbl = "bt_recvwait";
	if (bmc_io_wait(sc, &a) < 0)
		return (-1);

	bt_write(sc, _BT_CTRL_REG, BT_HOST_BUSY);
	bt_write(sc, _BT_CTRL_REG, BT_BMC2HOST_ATN);
	bt_write(sc, _BT_CTRL_REG, BT_CLR_RD_PTR);
	len = bt_read(sc, _BT_DATAIN_REG);
	for (i = IPMI_BTMSG_NFLN, j = 0; i <= len; i++) {
		v = bt_read(sc, _BT_DATAIN_REG);
		if (i != IPMI_BTMSG_SEQ)
			*(sc->sc_buf + j++) = v;
	}
	bt_write(sc, _BT_CTRL_REG, BT_HOST_BUSY);
	c->c_rxlen = len - 1;

	return (0);
}

int
bt_reset(struct ipmi_softc *sc)
{
	return (-1);
}

int
bt_probe(struct ipmi_softc *sc)
{
	u_int8_t rv;

	rv = bmc_read(sc, _BT_CTRL_REG);
	rv &= BT_HOST_BUSY;
	rv |= BT_CLR_WR_PTR|BT_CLR_RD_PTR|BT_BMC2HOST_ATN|BT_HOST2BMC_ATN;
	bmc_write(sc, _BT_CTRL_REG, rv);

	rv = bmc_read(sc, _BT_INTMASK_REG);
	rv &= BT_IM_SCI_EN|BT_IM_SMI_EN|BT_IM_NMI2SMI;
	rv |= BT_IM_HIRQ_PEND;
	bmc_write(sc, _BT_INTMASK_REG, rv);

#if 0
	printf("bt_probe: %2x\n", v);
	printf(" WR    : %2x\n", v & BT_CLR_WR_PTR);
	printf(" RD    : %2x\n", v & BT_CLR_RD_PTR);
	printf(" H2B   : %2x\n", v & BT_HOST2BMC_ATN);
	printf(" B2H   : %2x\n", v & BT_BMC2HOST_ATN);
	printf(" EVT   : %2x\n", v & BT_EVT_ATN);
	printf(" HBSY  : %2x\n", v & BT_HOST_BUSY);
	printf(" BBSY  : %2x\n", v & BT_BMC_BUSY);
#endif
	return (0);
}

/*
 * SMIC interface
 */
#define _SMIC_DATAIN_REG		0
#define _SMIC_DATAOUT_REG		0

#define _SMIC_CTRL_REG			1
#define	  SMS_CC_GET_STATUS		 0x40
#define	  SMS_CC_START_TRANSFER		 0x41
#define	  SMS_CC_NEXT_TRANSFER		 0x42
#define	  SMS_CC_END_TRANSFER		 0x43
#define	  SMS_CC_START_RECEIVE		 0x44
#define	  SMS_CC_NEXT_RECEIVE		 0x45
#define	  SMS_CC_END_RECEIVE		 0x46
#define	  SMS_CC_TRANSFER_ABORT		 0x47

#define	  SMS_SC_READY			 0xc0
#define	  SMS_SC_WRITE_START		 0xc1
#define	  SMS_SC_WRITE_NEXT		 0xc2
#define	  SMS_SC_WRITE_END		 0xc3
#define	  SMS_SC_READ_START		 0xc4
#define	  SMS_SC_READ_NEXT		 0xc5
#define	  SMS_SC_READ_END		 0xc6

#define _SMIC_FLAG_REG			2
#define	  SMIC_BUSY			(1L << 0)
#define	  SMIC_SMS_ATN			(1L << 2)
#define	  SMIC_EVT_ATN			(1L << 3)
#define	  SMIC_SMI			(1L << 4)
#define	  SMIC_TX_DATA_RDY		(1L << 6)
#define	  SMIC_RX_DATA_RDY		(1L << 7)

int	smic_wait(struct ipmi_softc *, u_int8_t, u_int8_t, const char *);
int	smic_write_cmd_data(struct ipmi_softc *, u_int8_t, const u_int8_t *);
int	smic_read_data(struct ipmi_softc *, u_int8_t *);

int
smic_wait(struct ipmi_softc *sc, u_int8_t mask, u_int8_t val, const char *lbl)
{
	struct ipmi_iowait a;
	int v;

	/* Wait for expected flag bits */
	a.offset = _SMIC_FLAG_REG;
	a.mask = mask;
	a.value = val;
	a.lbl = "smicwait";
	v = bmc_io_wait(sc, &a);
	if (v < 0)
		return (-1);

	/* Return current status */
	v = bmc_read(sc, _SMIC_CTRL_REG);
	dbg_printf(99, "smic_wait = %.2x\n", v);
	return (v);
}

int
smic_write_cmd_data(struct ipmi_softc *sc, u_int8_t cmd, const u_int8_t *data)
{
	int	sts, v;

	dbg_printf(50, "smic_wcd: %.2x %.2x\n", cmd, data ? *data : -1);
	sts = smic_wait(sc, SMIC_TX_DATA_RDY | SMIC_BUSY, SMIC_TX_DATA_RDY,
	    "smic_write_cmd_data ready");
	if (sts < 0)
		return (sts);

	bmc_write(sc, _SMIC_CTRL_REG, cmd);
	if (data)
		bmc_write(sc, _SMIC_DATAOUT_REG, *data);

	/* Toggle BUSY bit, then wait for busy bit to clear */
	v = bmc_read(sc, _SMIC_FLAG_REG);
	bmc_write(sc, _SMIC_FLAG_REG, v | SMIC_BUSY);

	return (smic_wait(sc, SMIC_BUSY, 0, "smic_write_cmd_data busy"));
}

int
smic_read_data(struct ipmi_softc *sc, u_int8_t *data)
{
	int sts;

	sts = smic_wait(sc, SMIC_RX_DATA_RDY | SMIC_BUSY, SMIC_RX_DATA_RDY,
	    "smic_read_data");
	if (sts >= 0) {
		*data = bmc_read(sc, _SMIC_DATAIN_REG);
		dbg_printf(50, "smic_readdata: %.2x\n", *data);
	}
	return (sts);
}

#define ErrStat(a,b) if (a) printf(b);

int
smic_sendmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	int sts, idx;

	sts = smic_write_cmd_data(sc, SMS_CC_START_TRANSFER, &sc->sc_buf[0]);
	ErrStat(sts != SMS_SC_WRITE_START, "wstart");
	for (idx = 1; idx < c->c_txlen - 1; idx++) {
		sts = smic_write_cmd_data(sc, SMS_CC_NEXT_TRANSFER,
		    &sc->sc_buf[idx]);
		ErrStat(sts != SMS_SC_WRITE_NEXT, "write");
	}
	sts = smic_write_cmd_data(sc, SMS_CC_END_TRANSFER, &sc->sc_buf[idx]);
	if (sts != SMS_SC_WRITE_END) {
		dbg_printf(50, "smic_sendmsg %d/%d = %.2x\n", idx, c->c_txlen, sts);
		return (-1);
	}

	return (0);
}

int
smic_recvmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	int sts, idx;

	c->c_rxlen = 0;
	sts = smic_wait(sc, SMIC_RX_DATA_RDY, SMIC_RX_DATA_RDY, "smic_recvmsg");
	if (sts < 0)
		return (-1);

	sts = smic_write_cmd_data(sc, SMS_CC_START_RECEIVE, NULL);
	ErrStat(sts != SMS_SC_READ_START, "rstart");
	for (idx = 0;; ) {
		sts = smic_read_data(sc, &sc->sc_buf[idx++]);
		if (sts != SMS_SC_READ_START && sts != SMS_SC_READ_NEXT)
			break;
		smic_write_cmd_data(sc, SMS_CC_NEXT_RECEIVE, NULL);
	}
	ErrStat(sts != SMS_SC_READ_END, "rend");

	c->c_rxlen = idx;

	sts = smic_write_cmd_data(sc, SMS_CC_END_RECEIVE, NULL);
	if (sts != SMS_SC_READY) {
		dbg_printf(50, "smic_recvmsg %d/%d = %.2x\n", idx, c->c_maxrxlen, sts);
		return (-1);
	}

	return (0);
}

int
smic_reset(struct ipmi_softc *sc)
{
	return (-1);
}

int
smic_probe(struct ipmi_softc *sc)
{
	/* Flag register should not be 0xFF on a good system */
	if (bmc_read(sc, _SMIC_FLAG_REG) == 0xFF)
		return (-1);

	return (0);
}

/*
 * KCS interface
 */
#define _KCS_DATAIN_REGISTER		0
#define _KCS_DATAOUT_REGISTER		0
#define	  KCS_READ_NEXT			0x68

#define _KCS_COMMAND_REGISTER		1
#define	  KCS_GET_STATUS		0x60
#define	  KCS_WRITE_START		0x61
#define	  KCS_WRITE_END			0x62

#define _KCS_STATUS_REGISTER		1
#define	  KCS_OBF			(1L << 0)
#define	  KCS_IBF			(1L << 1)
#define	  KCS_SMS_ATN			(1L << 2)
#define	  KCS_CD			(1L << 3)
#define	  KCS_OEM1			(1L << 4)
#define	  KCS_OEM2			(1L << 5)
#define	  KCS_STATE_MASK		0xc0
#define	    KCS_IDLE_STATE		0x00
#define	    KCS_READ_STATE		0x40
#define	    KCS_WRITE_STATE		0x80
#define	    KCS_ERROR_STATE		0xC0

int	kcs_wait(struct ipmi_softc *, u_int8_t, u_int8_t, const char *);
int	kcs_write_cmd(struct ipmi_softc *, u_int8_t);
int	kcs_write_data(struct ipmi_softc *, u_int8_t);
int	kcs_read_data(struct ipmi_softc *, u_int8_t *);

int
kcs_wait(struct ipmi_softc *sc, u_int8_t mask, u_int8_t value, const char *lbl)
{
	struct ipmi_iowait a;
	int v;

	a.offset = _KCS_STATUS_REGISTER;
	a.mask = mask;
	a.value = value;
	a.lbl = lbl;
	v = bmc_io_wait(sc, &a);
	if (v < 0)
		return (v);

	/* Check if output buffer full, read dummy byte	 */
	if ((v & (KCS_OBF | KCS_STATE_MASK)) == (KCS_OBF | KCS_WRITE_STATE))
		bmc_read(sc, _KCS_DATAIN_REGISTER);

	/* Check for error state */
	if ((v & KCS_STATE_MASK) == KCS_ERROR_STATE) {
		bmc_write(sc, _KCS_COMMAND_REGISTER, KCS_GET_STATUS);
		while (bmc_read(sc, _KCS_STATUS_REGISTER) & KCS_IBF)
			continue;
		printf("%s: error code: %x\n", DEVNAME(sc),
		    bmc_read(sc, _KCS_DATAIN_REGISTER));
	}

	return (v & KCS_STATE_MASK);
}

int
kcs_write_cmd(struct ipmi_softc *sc, u_int8_t cmd)
{
	/* ASSERT: IBF and OBF are clear */
	dbg_printf(50, "kcswritecmd: %.2x\n", cmd);
	bmc_write(sc, _KCS_COMMAND_REGISTER, cmd);

	return (kcs_wait(sc, KCS_IBF, 0, "write_cmd"));
}

int
kcs_write_data(struct ipmi_softc *sc, u_int8_t data)
{
	/* ASSERT: IBF and OBF are clear */
	dbg_printf(50, "kcswritedata: %.2x\n", data);
	bmc_write(sc, _KCS_DATAOUT_REGISTER, data);

	return (kcs_wait(sc, KCS_IBF, 0, "write_data"));
}

int
kcs_read_data(struct ipmi_softc *sc, u_int8_t * data)
{
	int sts;

	sts = kcs_wait(sc, KCS_IBF | KCS_OBF, KCS_OBF, "read_data");
	if (sts != KCS_READ_STATE)
		return (sts);

	/* ASSERT: OBF is set read data, request next byte */
	*data = bmc_read(sc, _KCS_DATAIN_REGISTER);
	bmc_write(sc, _KCS_DATAOUT_REGISTER, KCS_READ_NEXT);

	dbg_printf(50, "kcsreaddata: %.2x\n", *data);

	return (sts);
}

/* Exported KCS functions */
int
kcs_sendmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	int idx, sts;

	/* ASSERT: IBF is clear */
	dbg_dump(50, "kcs sendmsg", c->c_txlen, sc->sc_buf);
	sts = kcs_write_cmd(sc, KCS_WRITE_START);
	for (idx = 0; idx < c->c_txlen; idx++) {
		if (idx == c->c_txlen - 1)
			sts = kcs_write_cmd(sc, KCS_WRITE_END);

		if (sts != KCS_WRITE_STATE)
			break;

		sts = kcs_write_data(sc, sc->sc_buf[idx]);
	}
	if (sts != KCS_READ_STATE) {
		dbg_printf(1, "kcs sendmsg = %d/%d <%.2x>\n", idx, c->c_txlen, sts);
		dbg_dump(1, "kcs_sendmsg", c->c_txlen, sc->sc_buf);
		return (-1);
	}

	return (0);
}

int
kcs_recvmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	int idx, sts;

	for (idx = 0; idx < c->c_maxrxlen; idx++) {
		sts = kcs_read_data(sc, &sc->sc_buf[idx]);
		if (sts != KCS_READ_STATE)
			break;
	}
	sts = kcs_wait(sc, KCS_IBF, 0, "recv");
	c->c_rxlen = idx;
	if (sts != KCS_IDLE_STATE) {
		dbg_printf(1, "kcs recvmsg = %d/%d <%.2x>\n", idx, c->c_maxrxlen, sts);
		return (-1);
	}

	dbg_dump(50, "kcs recvmsg", idx, sc->sc_buf);

	return (0);
}

int
kcs_reset(struct ipmi_softc *sc)
{
	return (-1);
}

int
kcs_probe(struct ipmi_softc *sc)
{
	u_int8_t v;

	v = bmc_read(sc, _KCS_STATUS_REGISTER);
	if ((v & KCS_STATE_MASK) == KCS_ERROR_STATE)
		return (1);
#if 0
	printf("kcs_probe: %2x\n", v);
	printf(" STS: %2x\n", v & KCS_STATE_MASK);
	printf(" ATN: %2x\n", v & KCS_SMS_ATN);
	printf(" C/D: %2x\n", v & KCS_CD);
	printf(" IBF: %2x\n", v & KCS_IBF);
	printf(" OBF: %2x\n", v & KCS_OBF);
#endif
	return (0);
}

/*
 * IPMI code
 */
#define READ_SMS_BUFFER		0x37
#define WRITE_I2C		0x50

#define GET_MESSAGE_CMD		0x33
#define SEND_MESSAGE_CMD	0x34

#define IPMB_CHANNEL_NUMBER	0

#define PUBLIC_BUS		0

#define MIN_I2C_PACKET_SIZE	3
#define MIN_IMB_PACKET_SIZE	7	/* one byte for cksum */

#define MIN_BTBMC_REQ_SIZE	4
#define MIN_BTBMC_RSP_SIZE	5
#define MIN_BMC_REQ_SIZE	2
#define MIN_BMC_RSP_SIZE	3

#define BMC_SA			0x20	/* BMC/ESM3 */
#define FPC_SA			0x22	/* front panel */
#define BP_SA			0xC0	/* Primary Backplane */
#define BP2_SA			0xC2	/* Secondary Backplane */
#define PBP_SA			0xC4	/* Peripheral Backplane */
#define DRAC_SA			0x28	/* DRAC-III */
#define DRAC3_SA		0x30	/* DRAC-III */
#define BMC_LUN			0
#define SMS_LUN			2

struct ipmi_request {
	u_int8_t	rsSa;
	u_int8_t	rsLun;
	u_int8_t	netFn;
	u_int8_t	cmd;
	u_int8_t	data_len;
	u_int8_t	*data;
};

struct ipmi_response {
	u_int8_t	cCode;
	u_int8_t	data_len;
	u_int8_t	*data;
};

struct ipmi_bmc_request {
	u_int8_t	bmc_nfLn;
	u_int8_t	bmc_cmd;
	u_int8_t	bmc_data_len;
	u_int8_t	bmc_data[1];
};

struct ipmi_bmc_response {
	u_int8_t	bmc_nfLn;
	u_int8_t	bmc_cmd;
	u_int8_t	bmc_cCode;
	u_int8_t	bmc_data_len;
	u_int8_t	bmc_data[1];
};

struct cfdriver ipmi_cd = {
	NULL, "ipmi", DV_DULL
};

void
dumpb(const char *lbl, int len, const u_int8_t *data)
{
	int idx;

	printf("%s: ", lbl);
	for (idx = 0; idx < len; idx++)
		printf("%.2x ", data[idx]);

	printf("\n");
}

/*
 * bt_buildmsg builds an IPMI message from a nfLun, cmd, and data
 * This is used by BT protocol
 */
void
bt_buildmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	u_int8_t *buf = sc->sc_buf;

	buf[IPMI_BTMSG_LEN] = c->c_txlen + (IPMI_BTMSG_DATASND - 1);
	buf[IPMI_BTMSG_NFLN] = NETFN_LUN(c->c_netfn, c->c_rslun);
	buf[IPMI_BTMSG_SEQ] = sc->sc_btseq++;
	buf[IPMI_BTMSG_CMD] = c->c_cmd;
	if (c->c_txlen && c->c_data)
		memcpy(buf + IPMI_BTMSG_DATASND, c->c_data, c->c_txlen);
}

/*
 * cmn_buildmsg builds an IPMI message from a nfLun, cmd, and data
 * This is used by both SMIC and KCS protocols
 */
void
cmn_buildmsg(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	u_int8_t *buf = sc->sc_buf;

	buf[IPMI_MSG_NFLN] = NETFN_LUN(c->c_netfn, c->c_rslun);
	buf[IPMI_MSG_CMD] = c->c_cmd;
	if (c->c_txlen && c->c_data)
		memcpy(buf + IPMI_MSG_DATASND, c->c_data, c->c_txlen);
}

/* Send an IPMI command */
int
ipmi_sendcmd(struct ipmi_cmd *c)
{
	struct ipmi_softc	*sc = c->c_sc;
	int		rc = -1;

	dbg_printf(50, "ipmi_sendcmd: rssa=%.2x nfln=%.2x cmd=%.2x len=%.2x\n",
	    c->c_rssa, NETFN_LUN(c->c_netfn, c->c_rslun), c->c_cmd, c->c_txlen);
	dbg_dump(10, " send", c->c_txlen, c->c_data);
	if (c->c_rssa != BMC_SA) {
#if 0
		sc->sc_if->buildmsg(c);
		pI2C->bus = (sc->if_ver == 0x09) ?
		    PUBLIC_BUS :
		    IPMB_CHANNEL_NUMBER;

		imbreq->rsSa = rssa;
		imbreq->nfLn = NETFN_LUN(netfn, rslun);
		imbreq->cSum1 = -(imbreq->rsSa + imbreq->nfLn);
		imbreq->rqSa = BMC_SA;
		imbreq->seqLn = NETFN_LUN(sc->imb_seq++, SMS_LUN);
		imbreq->cmd = cmd;
		if (txlen)
			memcpy(imbreq->data, data, txlen);
		/* Set message checksum */
		imbreq->data[txlen] = cksum8(&imbreq->rqSa, txlen + 3);
#endif
		goto done;
	} else
		sc->sc_if->buildmsg(c);

	c->c_txlen += sc->sc_if->datasnd;
	rc = sc->sc_if->sendmsg(c);

done:
	return (rc);
}

/* Receive an IPMI command */
int
ipmi_recvcmd(struct ipmi_cmd *c)
{
	struct ipmi_softc *sc = c->c_sc;
	u_int8_t	*buf = sc->sc_buf, rc = 0;

	/* Receive message from interface, copy out result data */
	c->c_maxrxlen += sc->sc_if->datarcv;
	if (sc->sc_if->recvmsg(c) ||
	    c->c_rxlen < sc->sc_if->datarcv) {
		return (-1);
	}

	c->c_rxlen -= sc->sc_if->datarcv;
	if (c->c_rxlen > 0 && c->c_data)
		memcpy(c->c_data, buf + sc->sc_if->datarcv, c->c_rxlen);

	rc = buf[IPMI_MSG_CCODE];
#ifdef IPMI_DEBUG
	if (rc != 0)
		dbg_printf(1, "ipmi_recvcmd: nfln=%.2x cmd=%.2x err=%.2x\n",
		    buf[IPMI_MSG_NFLN], buf[IPMI_MSG_CMD], buf[IPMI_MSG_CCODE]);
#endif

	dbg_printf(50, "ipmi_recvcmd: nfln=%.2x cmd=%.2x err=%.2x len=%.2x\n",
	    buf[IPMI_MSG_NFLN], buf[IPMI_MSG_CMD], buf[IPMI_MSG_CCODE],
	    c->c_rxlen);
	dbg_dump(10, " recv", c->c_rxlen, c->c_data);

	return (rc);
}

void
ipmi_cmd(struct ipmi_cmd *c)
{
	if (cold || panicstr != NULL)
		ipmi_cmd_poll(c);
	else
		ipmi_cmd_wait(c);
}

void
ipmi_cmd_poll(struct ipmi_cmd *c)
{
	if ((c->c_ccode = ipmi_sendcmd(c)))
		printf("%s: sendcmd fails\n", DEVNAME(c->c_sc));
	else
		c->c_ccode = ipmi_recvcmd(c);
}

void
ipmi_cmd_wait(struct ipmi_cmd *c)
{
	struct task t;
	int res;

	task_set(&t, ipmi_cmd_wait_cb, c);
	res = task_add(c->c_sc->sc_cmd_taskq, &t);
	KASSERT(res == 1);

	tsleep_nsec(c, PWAIT, "ipmicmd", INFSLP);

	res = task_del(c->c_sc->sc_cmd_taskq, &t);
	KASSERT(res == 0);
}

void
ipmi_cmd_wait_cb(void *arg)
{
	struct ipmi_cmd *c = arg;

	ipmi_cmd_poll(c);
	wakeup(c);
}

/* Read a partial SDR entry */
int
get_sdr_partial(struct ipmi_softc *sc, u_int16_t recordId, u_int16_t reserveId,
    u_int8_t offset, u_int8_t length, void *buffer, u_int16_t *nxtRecordId)
{
	u_int8_t	cmd[IPMI_GET_WDOG_MAX + 255];	/* 8 + max of length */
	int		len;

	((u_int16_t *) cmd)[0] = reserveId;
	((u_int16_t *) cmd)[1] = recordId;
	cmd[4] = offset;
	cmd[5] = length;

	struct ipmi_cmd	c;
	c.c_sc = sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = STORAGE_NETFN;
	c.c_cmd = STORAGE_GET_SDR;
	c.c_txlen = IPMI_SET_WDOG_MAX;
	c.c_rxlen = 0;
	c.c_maxrxlen = 8 + length;
	c.c_data = cmd;
	ipmi_cmd(&c);
	len = c.c_rxlen;

	if (nxtRecordId)
		*nxtRecordId = *(uint16_t *) cmd;
	if (len > 2)
		memcpy(buffer, cmd + 2, len - 2);
	else
		return (1);

	return (0);
}

int maxsdrlen = 0x10;

/* Read an entire SDR; pass to add sensor */
int
get_sdr(struct ipmi_softc *sc, u_int16_t recid, u_int16_t *nxtrec)
{
	u_int16_t	resid = 0;
	int		len, sdrlen, offset;
	u_int8_t	*psdr;
	struct sdrhdr	shdr;

	/* Reserve SDR */
	struct ipmi_cmd	c;
	c.c_sc = sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = STORAGE_NETFN;
	c.c_cmd = STORAGE_RESERVE_SDR;
	c.c_txlen = 0;
	c.c_maxrxlen = sizeof(resid);
	c.c_rxlen = 0;
	c.c_data = &resid;
	ipmi_cmd(&c);

	/* Get SDR Header */
	if (get_sdr_partial(sc, recid, resid, 0, sizeof shdr, &shdr, nxtrec)) {
		printf("%s: get header fails\n", DEVNAME(sc));
		return (1);
	}
	/* Allocate space for entire SDR Length of SDR in header does not
	 * include header length */
	sdrlen = sizeof(shdr) + shdr.record_length;
	psdr = malloc(sdrlen, M_DEVBUF, M_NOWAIT);
	if (psdr == NULL)
		return (1);

	memcpy(psdr, &shdr, sizeof(shdr));

	/* Read SDR Data maxsdrlen bytes at a time */
	for (offset = sizeof(shdr); offset < sdrlen; offset += maxsdrlen) {
		len = sdrlen - offset;
		if (len > maxsdrlen)
			len = maxsdrlen;

		if (get_sdr_partial(sc, recid, resid, offset, len,
		    psdr + offset, NULL)) {
			printf("%s: get chunk: %d,%d fails\n", DEVNAME(sc),
			    offset, len);
			free(psdr, M_DEVBUF, sdrlen);
			return (1);
		}
	}

	/* Add SDR to sensor list, if not wanted, free buffer */
	if (add_sdr_sensor(sc, psdr, sdrlen) == 0)
		free(psdr, M_DEVBUF, sdrlen);

	return (0);
}

int
getbits(u_int8_t *bytes, int bitpos, int bitlen)
{
	int	v;
	int	mask;

	bitpos += bitlen - 1;
	for (v = 0; bitlen--;) {
		v <<= 1;
		mask = 1L << (bitpos & 7);
		if (bytes[bitpos >> 3] & mask)
			v |= 1;
		bitpos--;
	}

	return (v);
}

/* Decode IPMI sensor name */
int
ipmi_sensor_name(char *name, int len, u_int8_t typelen, u_int8_t *bits,
    int bitslen)
{
	int	i, slen;
	char	bcdplus[] = "0123456789 -.:,_";

	slen = typelen & 0x1F;
	switch (typelen >> 6) {
	case IPMI_NAME_UNICODE:
		//unicode
		break;

	case IPMI_NAME_BCDPLUS:
		/* Characters are encoded in 4-bit BCDPLUS */
		if (len < slen * 2 + 1)
			slen = (len >> 1) - 1;
		if (slen > bitslen)
			return (0);
		for (i = 0; i < slen; i++) {
			*(name++) = bcdplus[bits[i] >> 4];
			*(name++) = bcdplus[bits[i] & 0xF];
		}
		break;

	case IPMI_NAME_ASCII6BIT:
		/* Characters are encoded in 6-bit ASCII
		 *   0x00 - 0x3F maps to 0x20 - 0x5F */
		/* XXX: need to calculate max len: slen = 3/4 * len */
		if (len < slen + 1)
			slen = len - 1;
		if (slen * 6 / 8 > bitslen)
			return (0);
		for (i = 0; i < slen * 8; i += 6) {
			*(name++) = getbits(bits, i, 6) + ' ';
		}
		break;

	case IPMI_NAME_ASCII8BIT:
		/* Characters are 8-bit ascii */
		if (len < slen + 1)
			slen = len - 1;
		if (slen > bitslen)
			return (0);
		while (slen--)
			*(name++) = *(bits++);
		break;
	}
	*name = 0;

	return (1);
}

/* Calculate val * 10^exp */
long
ipow(long val, int exp)
{
	while (exp > 0) {
		val *= 10;
		exp--;
	}

	while (exp < 0) {
		val /= 10;
		exp++;
	}

	return (val);
}

/* Sign extend a n-bit value */
long
signextend(unsigned long val, int bits)
{
	long msk = (1L << (bits-1))-1;

	return (-(val & ~msk) | val);
}

/* Convert IPMI reading from sensor factors */
long
ipmi_convert(u_int8_t v, struct sdrtype1 *s1, long adj)
{
	int16_t	M, B;
	int8_t	K1, K2;
	long	val;

	/* Calculate linear reading variables */
	M  = signextend((((short)(s1->m_tolerance & 0xC0)) << 2) + s1->m, 10);
	B  = signextend((((short)(s1->b_accuracy & 0xC0)) << 2) + s1->b, 10);
	K1 = signextend(s1->rbexp & 0xF, 4);
	K2 = signextend(s1->rbexp >> 4, 4);

	/* Calculate sensor reading:
	 *  y = L((M * v + (B * 10^K1)) * 10^(K2+adj)
	 *
	 * This commutes out to:
	 *  y = L(M*v * 10^(K2+adj) + B * 10^(K1+K2+adj)); */
	val = ipow(M * v, K2 + adj) + ipow(B, K1 + K2 + adj);

	/* Linearization function: y = f(x) 0 : y = x 1 : y = ln(x) 2 : y =
	 * log10(x) 3 : y = log2(x) 4 : y = e^x 5 : y = 10^x 6 : y = 2^x 7 : y
	 * = 1/x 8 : y = x^2 9 : y = x^3 10 : y = square root(x) 11 : y = cube
	 * root(x) */
	return (val);
}

int
ipmi_sensor_status(struct ipmi_softc *sc, struct ipmi_sensor *psensor,
    u_int8_t *reading)
{
	struct sdrtype1	*s1 = (struct sdrtype1 *)psensor->i_sdr;
	int		etype;

	/* Get reading of sensor */
	switch (psensor->i_sensor.type) {
	case SENSOR_TEMP:
		psensor->i_sensor.value = ipmi_convert(reading[0], s1, 6);
		psensor->i_sensor.value += 273150000;
		break;

	case SENSOR_VOLTS_DC:
	case SENSOR_VOLTS_AC:
	case SENSOR_AMPS:
	case SENSOR_WATTS:
		psensor->i_sensor.value = ipmi_convert(reading[0], s1, 6);
		break;

	case SENSOR_FANRPM:
		psensor->i_sensor.value = ipmi_convert(reading[0], s1, 0);
		if (((s1->units1>>3)&0x7) == 0x3)
			psensor->i_sensor.value *= 60; // RPS -> RPM
		break;
	default:
		break;
	}

	/* Return Sensor Status */
	etype = (psensor->etype << 8) + psensor->stype;
	switch (etype) {
	case IPMI_SENSOR_TYPE_TEMP:
	case IPMI_SENSOR_TYPE_VOLT:
	case IPMI_SENSOR_TYPE_CURRENT:
	case IPMI_SENSOR_TYPE_FAN:
		/* non-recoverable threshold */
		if (reading[2] & ((1 << 5) | (1 << 2)))
			return (SENSOR_S_CRIT);
		/* critical threshold */
		else if (reading[2] & ((1 << 4) | (1 << 1)))
			return (SENSOR_S_CRIT);
		/* non-critical threshold */
		else if (reading[2] & ((1 << 3) | (1 << 0)))
			return (SENSOR_S_WARN);
		break;

	case IPMI_SENSOR_TYPE_INTRUSION:
		psensor->i_sensor.value = (reading[2] & 1) ? 1 : 0;
		if (reading[2] & 0x1)
			return (SENSOR_S_CRIT);
		break;

	case IPMI_SENSOR_TYPE_PWRSUPPLY:
		/* Reading: 1 = present+powered, 0 = otherwise */
		psensor->i_sensor.value = (reading[2] & 1) ? 1 : 0;
		if (reading[2] & 0x10) {
			/* XXX: Need sysctl type for Power Supply types
			 *   ok: power supply installed && powered
			 * warn: power supply installed && !powered
			 * crit: power supply !installed
			 */
			return (SENSOR_S_CRIT);
		}
		if (reading[2] & 0x08) {
			/* Power supply AC lost */
			return (SENSOR_S_WARN);
		}
		break;
	}

	return (SENSOR_S_OK);
}

int
read_sensor(struct ipmi_softc *sc, struct ipmi_sensor *psensor)
{
	struct sdrtype1	*s1 = (struct sdrtype1 *) psensor->i_sdr;
	u_int8_t	data[8];
	int		rv = -1;

	memset(data, 0, sizeof(data));
	data[0] = psensor->i_num;

	struct ipmi_cmd	c;
	c.c_sc = sc;
	c.c_rssa = s1->owner_id;
	c.c_rslun = s1->owner_lun;
	c.c_netfn = SE_NETFN;
	c.c_cmd = SE_GET_SENSOR_READING;
	c.c_txlen = 1;
	c.c_maxrxlen = sizeof(data);
	c.c_rxlen = 0;
	c.c_data = data;
	ipmi_cmd(&c);

	if (c.c_ccode != 0) {
		dbg_printf(1, "sensor reading command for %s failed: %.2x\n",
			psensor->i_sensor.desc, c.c_ccode);
		return (rv);
	}
	dbg_printf(10, "values=%.2x %.2x %.2x %.2x %s\n",
	    data[0],data[1],data[2],data[3], psensor->i_sensor.desc);
	psensor->i_sensor.flags &= ~SENSOR_FINVALID;
	if ((data[1] & IPMI_INVALID_SENSOR) ||
	    ((data[1] & IPMI_DISABLED_SENSOR) == 0 && data[0] == 0))
		psensor->i_sensor.flags |= SENSOR_FINVALID;
	psensor->i_sensor.status = ipmi_sensor_status(sc, psensor, data);
	rv = 0;
	return (rv);
}

int
ipmi_sensor_type(int type, int ext_type, int units2, int entity)
{
	switch (units2) {
	case IPMI_UNIT_TYPE_AMPS:
		return (SENSOR_AMPS);

	case IPMI_UNIT_TYPE_RPM:
		return (SENSOR_FANRPM);

	/* XXX sensors framework distinguishes AC/DC but ipmi does not */
	case IPMI_UNIT_TYPE_VOLTS:
		return (SENSOR_VOLTS_DC);

	case IPMI_UNIT_TYPE_WATTS:
		return (SENSOR_WATTS);
	}

	switch (ext_type << 8L | type) {
	case IPMI_SENSOR_TYPE_TEMP:
		return (SENSOR_TEMP);

	case IPMI_SENSOR_TYPE_PWRSUPPLY:
		if (entity == IPMI_ENTITY_PWRSUPPLY)
			return (SENSOR_INDICATOR);
		break;

	case IPMI_SENSOR_TYPE_INTRUSION:
		return (SENSOR_INDICATOR);
	}

	return (-1);
}

/* Add Sensor to BSD Sysctl interface */
int
add_sdr_sensor(struct ipmi_softc *sc, u_int8_t *psdr, int sdrlen)
{
	int			rc;
	struct sdrtype1		*s1 = (struct sdrtype1 *)psdr;
	struct sdrtype2		*s2 = (struct sdrtype2 *)psdr;
	char			name[64];

	switch (s1->sdrhdr.record_type) {
	case IPMI_SDR_TYPEFULL:
		rc = ipmi_sensor_name(name, sizeof(name), s1->typelen,
		    s1->name, sdrlen - (int)offsetof(struct sdrtype1, name));
		if (rc == 0)
			return (0);
		rc = add_child_sensors(sc, psdr, 1, s1->sensor_num,
		    s1->sensor_type, s1->event_code, 0, s1->entity_id, name);
		break;

	case IPMI_SDR_TYPECOMPACT:
		rc = ipmi_sensor_name(name, sizeof(name), s2->typelen,
		    s2->name, sdrlen - (int)offsetof(struct sdrtype2, name));
		if (rc == 0)
			return (0);
		rc = add_child_sensors(sc, psdr, s2->share1 & 0xF,
		    s2->sensor_num, s2->sensor_type, s2->event_code,
		    s2->share2 & 0x7F, s2->entity_id, name);
		break;

	default:
		return (0);
	}

	return rc;
}

int
add_child_sensors(struct ipmi_softc *sc, u_int8_t *psdr, int count,
    int sensor_num, int sensor_type, int ext_type, int sensor_base,
    int entity, const char *name)
{
	int			typ, idx, rc = 0;
	struct ipmi_sensor	*psensor;
	struct sdrtype1		*s1 = (struct sdrtype1 *)psdr;

	typ = ipmi_sensor_type(sensor_type, ext_type, s1->units2, entity);
	if (typ == -1) {
		dbg_printf(5, "Unknown sensor type:%.2x et:%.2x sn:%.2x "
		    "units2:%u name:%s\n", sensor_type, ext_type, sensor_num,
		    s1->units2, name);
		return 0;
	}
	for (idx = 0; idx < count; idx++) {
		psensor = malloc(sizeof(*psensor), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (psensor == NULL)
			break;

		/* Initialize BSD Sensor info */
		psensor->i_sdr = psdr;
		psensor->i_num = sensor_num + idx;
		psensor->stype = sensor_type;
		psensor->etype = ext_type;
		psensor->i_sensor.type = typ;
		if (count > 1)
			snprintf(psensor->i_sensor.desc,
			    sizeof(psensor->i_sensor.desc),
			    "%s - %d", name, sensor_base + idx);
		else
			strlcpy(psensor->i_sensor.desc, name,
			    sizeof(psensor->i_sensor.desc));

		dbg_printf(5, "add sensor:%.4x %.2x:%d ent:%.2x:%.2x %s\n",
		    s1->sdrhdr.record_id, s1->sensor_type,
		    typ, s1->entity_id, s1->entity_instance,
		    psensor->i_sensor.desc);
		if (read_sensor(sc, psensor) == 0) {
			SLIST_INSERT_HEAD(&ipmi_sensor_list, psensor, list);
			sensor_attach(&sc->sc_sensordev, &psensor->i_sensor);
			dbg_printf(5, "	 reading: %lld [%s]\n",
			    psensor->i_sensor.value,
			    psensor->i_sensor.desc);
			rc = 1;
		} else
			free(psensor, M_DEVBUF, sizeof(*psensor));
	}

	return (rc);
}

/* Handle IPMI Timer - reread sensor values */
void
ipmi_refresh_sensors(struct ipmi_softc *sc)
{
	if (SLIST_EMPTY(&ipmi_sensor_list))
		return;

	sc->current_sensor = SLIST_NEXT(sc->current_sensor, list);
	if (sc->current_sensor == NULL)
		sc->current_sensor = SLIST_FIRST(&ipmi_sensor_list);

	if (read_sensor(sc, sc->current_sensor)) {
		dbg_printf(1, "%s: error reading: %s\n", DEVNAME(sc),
		    sc->current_sensor->i_sensor.desc);
		return;
	}
}

int
ipmi_map_regs(struct ipmi_softc *sc, struct ipmi_attach_args *ia)
{
	if (sc->sc_if && sc->sc_if->nregs == 0)
		return (0);

	sc->sc_if = ipmi_get_if(ia->iaa_if_type);
	if (sc->sc_if == NULL)
		return (-1);

	if (ia->iaa_if_iotype == 'i')
		sc->sc_iot = ia->iaa_iot;
	else
		sc->sc_iot = ia->iaa_memt;

	sc->sc_if_rev = ia->iaa_if_rev;
	sc->sc_if_iosize = ia->iaa_if_iosize;
	sc->sc_if_iospacing = ia->iaa_if_iospacing;
	if (bus_space_map(sc->sc_iot, ia->iaa_if_iobase,
	    sc->sc_if->nregs * sc->sc_if_iospacing,
	    0, &sc->sc_ioh)) {
		printf("%s: bus_space_map(%lx %lx %x 0 %p) failed\n",
		    DEVNAME(sc),
		    (unsigned long)sc->sc_iot, ia->iaa_if_iobase,
		    sc->sc_if->nregs * sc->sc_if_iospacing, &sc->sc_ioh);
		return (-1);
	}
	return (0);
}

void
ipmi_unmap_regs(struct ipmi_softc *sc)
{
	if (sc->sc_if->nregs > 0) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh,
		    sc->sc_if->nregs * sc->sc_if_iospacing);
	}
}

void
ipmi_poll_thread(void *arg)
{
	struct ipmi_thread	*thread = arg;
	struct ipmi_softc	*sc = thread->sc;
	u_int16_t		rec;

	/* Scan SDRs, add sensors */
	for (rec = 0; rec != 0xFFFF;) {
		if (get_sdr(sc, rec, &rec)) {
			ipmi_unmap_regs(sc);
			printf("%s: no SDRs IPMI disabled\n", DEVNAME(sc));
			goto done;
		}
		tsleep_nsec(sc, PWAIT, "ipmirun", MSEC_TO_NSEC(1));
	}

	/* initialize sensor list for thread */
	if (SLIST_EMPTY(&ipmi_sensor_list))
		goto done;
	else
		sc->current_sensor = SLIST_FIRST(&ipmi_sensor_list);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);

	while (thread->running) {
		ipmi_refresh_sensors(sc);
		tsleep_nsec(thread, PWAIT, "ipmi_poll",
		    SEC_TO_NSEC(SENSOR_REFRESH_RATE));
	}

done:
	kthread_exit(0);
}

void
ipmi_create_thread(void *arg)
{
	struct ipmi_softc	*sc = arg;

	if (kthread_create(ipmi_poll_thread, sc->sc_thread, NULL,
	    DEVNAME(sc)) != 0) {
		printf("%s: unable to create run thread, ipmi disabled\n",
		    DEVNAME(sc));
		return;
	}
}

void
ipmi_attach_common(struct ipmi_softc *sc, struct ipmi_attach_args *ia)
{
	struct ipmi_cmd		*c = &sc->sc_ioctl.cmd;

	/* Map registers */
	ipmi_map_regs(sc, ia);

	sc->sc_thread = malloc(sizeof(struct ipmi_thread), M_DEVBUF, M_NOWAIT);
	if (sc->sc_thread == NULL) {
		printf(": unable to allocate thread\n");
		return;
	}
	sc->sc_thread->sc = sc;
	sc->sc_thread->running = 1;

	/* Setup threads */
	kthread_create_deferred(ipmi_create_thread, sc);

	printf(": version %d.%d interface %s",
	    ia->iaa_if_rev >> 4, ia->iaa_if_rev & 0xF, sc->sc_if->name);
	if (sc->sc_if->nregs > 0)
		printf(" %sbase 0x%lx/%x spacing %d",
		    ia->iaa_if_iotype == 'i' ? "io" : "mem", ia->iaa_if_iobase,
		    ia->iaa_if_iospacing * sc->sc_if->nregs,
		    ia->iaa_if_iospacing);
	if (ia->iaa_if_irq != -1)
		printf(" irq %d", ia->iaa_if_irq);
	printf("\n");

	/* setup flag to exclude iic */
	ipmi_enabled = 1;

	/* Setup Watchdog timer */
	sc->sc_wdog_period = 0;
	task_set(&sc->sc_wdog_tickle_task, ipmi_watchdog_tickle, sc);
	wdog_register(ipmi_watchdog, sc);

	rw_init(&sc->sc_ioctl.lock, DEVNAME(sc));
	sc->sc_ioctl.req.msgid = -1;
	c->c_sc = sc;
	c->c_ccode = -1;

	sc->sc_cmd_taskq = taskq_create("ipmicmd", 1, IPL_MPFLOOR,
	    TASKQ_MPSAFE);
}

int
ipmi_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

struct ipmi_softc *
ipmilookup(dev_t dev)
{
	return (struct ipmi_softc *)device_lookup(&ipmi_cd, minor(dev));
}

int
ipmiopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct ipmi_softc	*sc = ipmilookup(dev);

	if (sc == NULL)
		return (ENXIO);
	return (0);
}

int
ipmiclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct ipmi_softc	*sc = ipmilookup(dev);

	if (sc == NULL)
		return (ENXIO);
	return (0);
}

int
ipmiioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *proc)
{
	struct ipmi_softc	*sc = ipmilookup(dev);
	struct ipmi_req		*req = (struct ipmi_req *)data;
	struct ipmi_recv	*recv = (struct ipmi_recv *)data;
	struct ipmi_cmd		*c = &sc->sc_ioctl.cmd;
	int			iv;
	int			len;
	u_char			ccode;
	int			rc = 0;

	if (sc == NULL)
		return (ENXIO);

	rw_enter_write(&sc->sc_ioctl.lock);

	c->c_maxrxlen = sizeof(sc->sc_ioctl.buf);
	c->c_data = sc->sc_ioctl.buf;

	switch (cmd) {
	case IPMICTL_SEND_COMMAND:
		if (req->msgid == -1) {
			rc = EINVAL;
			goto reset;
		}
		if (sc->sc_ioctl.req.msgid != -1) {
			rc = EBUSY;
			goto reset;
		}
		len = req->msg.data_len;
		if (len < 0) {
			rc = EINVAL;
			goto reset;
		}
		if (len > c->c_maxrxlen) {
			rc = E2BIG;
			goto reset;
		}
		sc->sc_ioctl.req = *req;
		c->c_ccode = -1;
		rc = copyin(req->msg.data, c->c_data, len);
		if (rc != 0)
			goto reset;
		KASSERT(c->c_ccode == -1);

		/* Execute a command synchronously. */
		c->c_netfn = req->msg.netfn;
		c->c_cmd = req->msg.cmd;
		c->c_txlen = req->msg.data_len;
		c->c_rxlen = 0;
		ipmi_cmd(c);
		break;
	case IPMICTL_RECEIVE_MSG_TRUNC:
	case IPMICTL_RECEIVE_MSG:
		if (sc->sc_ioctl.req.msgid == -1) {
			rc = EINVAL;
			goto reset;
		}
		if (c->c_ccode == -1) {
			rc = EAGAIN;
			goto reset;
		}
		ccode = c->c_ccode & 0xff;
		rc = copyout(&ccode, recv->msg.data, 1);
		if (rc != 0)
			goto reset;

		/* Return a command result. */
		recv->recv_type = IPMI_RESPONSE_RECV_TYPE;
		recv->msgid = sc->sc_ioctl.req.msgid;
		recv->msg.netfn = sc->sc_ioctl.req.msg.netfn;
		recv->msg.cmd = sc->sc_ioctl.req.msg.cmd;
		recv->msg.data_len = c->c_rxlen + 1;

		rc = copyout(c->c_data, recv->msg.data + 1, c->c_rxlen);
		/* Always reset state after command completion. */
		goto reset;
	case IPMICTL_SET_MY_ADDRESS_CMD:
		iv = *(int *)data;
		if (iv < 0 || iv > RSSA_MASK) {
			rc = EINVAL;
			goto reset;
		}
		c->c_rssa = iv;
		break;
	case IPMICTL_GET_MY_ADDRESS_CMD:
		*(int *)data = c->c_rssa;
		break;
	case IPMICTL_SET_MY_LUN_CMD:
		iv = *(int *)data;
		if (iv < 0 || iv > LUN_MASK) {
			rc = EINVAL;
			goto reset;
		}
		c->c_rslun = iv;
		break;
	case IPMICTL_GET_MY_LUN_CMD:
		*(int *)data = c->c_rslun;
		break;
	case IPMICTL_SET_GETS_EVENTS_CMD:
		break;
	case IPMICTL_REGISTER_FOR_CMD:
	case IPMICTL_UNREGISTER_FOR_CMD:
	default:
		break;
	}
done:
	rw_exit_write(&sc->sc_ioctl.lock);
	return (rc);
reset:
	sc->sc_ioctl.req.msgid = -1;
	c->c_ccode = -1;
	goto done;
}

#define		MIN_PERIOD	10

int
ipmi_watchdog(void *arg, int period)
{
	struct ipmi_softc	*sc = arg;

	if (sc->sc_wdog_period == period) {
		if (period != 0) {
			struct task *t;
			int res;

			t = &sc->sc_wdog_tickle_task;
			(void)task_del(systq, t);
			res = task_add(systq, t);
			KASSERT(res == 1);
		}
		return (period);
	}

	if (period < MIN_PERIOD && period > 0)
		period = MIN_PERIOD;
	sc->sc_wdog_period = period;
	ipmi_watchdog_set(sc);
	printf("%s: watchdog %sabled\n", DEVNAME(sc),
	    (period == 0) ? "dis" : "en");
	return (period);
}

void
ipmi_watchdog_tickle(void *arg)
{
	struct ipmi_softc	*sc = arg;
	struct ipmi_cmd		c;

	c.c_sc = sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = APP_NETFN;
	c.c_cmd = APP_RESET_WATCHDOG;
	c.c_txlen = 0;
	c.c_maxrxlen = 0;
	c.c_rxlen = 0;
	c.c_data = NULL;
	ipmi_cmd(&c);
}

void
ipmi_watchdog_set(void *arg)
{
	struct ipmi_softc	*sc = arg;
	uint8_t			wdog[IPMI_GET_WDOG_MAX];
	struct ipmi_cmd		c;

	c.c_sc = sc;
	c.c_rssa = BMC_SA;
	c.c_rslun = BMC_LUN;
	c.c_netfn = APP_NETFN;
	c.c_cmd = APP_GET_WATCHDOG_TIMER;
	c.c_txlen = 0;
	c.c_maxrxlen = IPMI_GET_WDOG_MAX;
	c.c_rxlen = 0;
	c.c_data = wdog;
	ipmi_cmd(&c);

	/* Period is 10ths/sec */
	uint16_t timo = htole16(sc->sc_wdog_period * 10);

	memcpy(&wdog[IPMI_SET_WDOG_TIMOL], &timo, 2);
	wdog[IPMI_SET_WDOG_TIMER] &= ~IPMI_WDOG_DONTSTOP;
	wdog[IPMI_SET_WDOG_TIMER] |= (sc->sc_wdog_period == 0) ?
	    0 : IPMI_WDOG_DONTSTOP;
	wdog[IPMI_SET_WDOG_ACTION] &= ~IPMI_WDOG_MASK;
	wdog[IPMI_SET_WDOG_ACTION] |= (sc->sc_wdog_period == 0) ?
	    IPMI_WDOG_DISABLED : IPMI_WDOG_REBOOT;

	c.c_cmd = APP_SET_WATCHDOG_TIMER;
	c.c_txlen = IPMI_SET_WDOG_MAX;
	c.c_maxrxlen = 0;
	c.c_rxlen = 0;
	c.c_data = wdog;
	ipmi_cmd(&c);
}

#if defined(__amd64__) || defined(__i386__)

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

/*
 * Format of SMBIOS IPMI Flags
 *
 * bit0: interrupt trigger mode (1=level, 0=edge)
 * bit1: interrupt polarity (1=active high, 0=active low)
 * bit2: reserved
 * bit3: address LSB (1=odd,0=even)
 * bit4: interrupt (1=specified, 0=not specified)
 * bit5: reserved
 * bit6/7: register spacing (1,4,2,err)
 */
#define SMIPMI_FLAG_IRQLVL		(1L << 0)
#define SMIPMI_FLAG_IRQEN		(1L << 3)
#define SMIPMI_FLAG_ODDOFFSET		(1L << 4)
#define SMIPMI_FLAG_IFSPACING(x)	(((x)>>6)&0x3)
#define	 IPMI_IOSPACING_BYTE		 0
#define	 IPMI_IOSPACING_WORD		 2
#define	 IPMI_IOSPACING_DWORD		 1

struct dmd_ipmi {
	u_int8_t	dmd_sig[4];		/* Signature 'IPMI' */
	u_int8_t	dmd_i2c_address;	/* Address of BMC */
	u_int8_t	dmd_nvram_address;	/* Address of NVRAM */
	u_int8_t	dmd_if_type;		/* IPMI Interface Type */
	u_int8_t	dmd_if_rev;		/* IPMI Interface Revision */
} __packed;

void	*scan_sig(long, long, int, int, const void *);

void	ipmi_smbios_probe(struct smbios_ipmi *, struct ipmi_attach_args *);
int	ipmi_match(struct device *, void *, void *);
void	ipmi_attach(struct device *, struct device *, void *);

const struct cfattach ipmi_ca = {
	sizeof(struct ipmi_softc), ipmi_match, ipmi_attach,
	NULL, ipmi_activate
};

int
ipmi_match(struct device *parent, void *match, void *aux)
{
	struct ipmi_softc	*sc;
	struct ipmi_attach_args *ia = aux;
	struct cfdata		*cf = match;
	u_int8_t		cmd[32];
	int			rv = 0;

	if (strcmp(ia->iaa_name, cf->cf_driver->cd_name))
		return (0);

	/* XXX local softc is wrong wrong wrong */
	sc = malloc(sizeof(*sc), M_TEMP, M_WAITOK | M_ZERO);
	strlcpy(sc->sc_dev.dv_xname, "ipmi0", sizeof(sc->sc_dev.dv_xname));

	/* Map registers */
	if (ipmi_map_regs(sc, ia) == 0) {
		sc->sc_if->probe(sc);

		/* Identify BMC device early to detect lying bios */
		struct ipmi_cmd c;
		c.c_sc = sc;
		c.c_rssa = BMC_SA;
		c.c_rslun = BMC_LUN;
		c.c_netfn = APP_NETFN;
		c.c_cmd = APP_GET_DEVICE_ID;
		c.c_txlen = 0;
		c.c_maxrxlen = sizeof(cmd);
		c.c_rxlen = 0;
		c.c_data = cmd;
		ipmi_cmd(&c);

		dbg_dump(1, "bmc data", c.c_rxlen, cmd);
		rv = 1; /* GETID worked, we got IPMI */
		ipmi_unmap_regs(sc);
	}

	free(sc, M_TEMP, sizeof(*sc));

	return (rv);
}

void
ipmi_attach(struct device *parent, struct device *self, void *aux)
{
	ipmi_attach_common((struct ipmi_softc *)self, aux);
}

/* Scan memory for signature */
void *
scan_sig(long start, long end, int skip, int len, const void *data)
{
	void *va;

	while (start < end) {
		va = ISA_HOLE_VADDR(start);
		if (memcmp(va, data, len) == 0)
			return (va);

		start += skip;
	}

	return (NULL);
}

void
ipmi_smbios_probe(struct smbios_ipmi *pipmi, struct ipmi_attach_args *ia)
{

	dbg_printf(1, "ipmi_smbios_probe: %02x %02x %02x %02x %08llx %02x "
	    "%02x\n",
	    pipmi->smipmi_if_type,
	    pipmi->smipmi_if_rev,
	    pipmi->smipmi_i2c_address,
	    pipmi->smipmi_nvram_address,
	    pipmi->smipmi_base_address,
	    pipmi->smipmi_base_flags,
	    pipmi->smipmi_irq);

	ia->iaa_if_type = pipmi->smipmi_if_type;
	ia->iaa_if_rev = pipmi->smipmi_if_rev;
	ia->iaa_if_irq = (pipmi->smipmi_base_flags & SMIPMI_FLAG_IRQEN) ?
	    pipmi->smipmi_irq : -1;
	ia->iaa_if_irqlvl = (pipmi->smipmi_base_flags & SMIPMI_FLAG_IRQLVL) ?
	    IST_LEVEL : IST_EDGE;
	ia->iaa_if_iosize = 1;

	switch (SMIPMI_FLAG_IFSPACING(pipmi->smipmi_base_flags)) {
	case IPMI_IOSPACING_BYTE:
		ia->iaa_if_iospacing = 1;
		break;

	case IPMI_IOSPACING_DWORD:
		ia->iaa_if_iospacing = 4;
		break;

	case IPMI_IOSPACING_WORD:
		ia->iaa_if_iospacing = 2;
		break;

	default:
		ia->iaa_if_iospacing = 1;
		printf("ipmi: unknown register spacing\n");
	}

	/* Calculate base address (PCI BAR format) */
	if (pipmi->smipmi_base_address & 0x1) {
		ia->iaa_if_iotype = 'i';
		ia->iaa_if_iobase = pipmi->smipmi_base_address & ~0x1;
	} else {
		ia->iaa_if_iotype = 'm';
		ia->iaa_if_iobase = pipmi->smipmi_base_address & ~0xF;
	}
	if (pipmi->smipmi_base_flags & SMIPMI_FLAG_ODDOFFSET)
		ia->iaa_if_iobase++;

	if (pipmi->smipmi_base_flags == 0x7f) {
		/* IBM 325 eServer workaround */
		ia->iaa_if_iospacing = 1;
		ia->iaa_if_iobase = pipmi->smipmi_base_address;
		ia->iaa_if_iotype = 'i';
		return;
	}
}

int
ipmi_probe(void *aux)
{
	struct ipmi_attach_args *ia = aux;
	struct dmd_ipmi *pipmi;
	struct smbtable tbl;

	tbl.cookie = 0;
	if (smbios_find_table(SMBIOS_TYPE_IPMIDEV, &tbl))
		ipmi_smbios_probe(tbl.tblhdr, ia);
	else {
		pipmi = (struct dmd_ipmi *)scan_sig(0xC0000L, 0xFFFFFL, 16, 4,
		    "IPMI");
		/* XXX hack to find Dell PowerEdge 8450 */
		if (pipmi == NULL) {
			/* no IPMI found */
			return (0);
		}

		/* we have an IPMI signature, fill in attach arg structure */
		ia->iaa_if_type = pipmi->dmd_if_type;
		ia->iaa_if_rev = pipmi->dmd_if_rev;
	}

	return (1);
}

#endif
