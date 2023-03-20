/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	pata_parport.h	(c) 1997-8  Grant R. Guenther <grant@torque.net>
 *				    Under the terms of the GPL.
 *
 * This file defines the interface for parallel port IDE adapter chip drivers.
 */

#ifndef LINUX_PATA_PARPORT_H
#define LINUX_PATA_PARPORT_H

#include <linux/libata.h>

#define PI_PCD	1	/* dummy for paride protocol modules */

struct pi_adapter {
	struct device dev;
	struct pi_protocol *proto;	/* adapter protocol */
	int port;			/* base address of parallel port */
	int mode;			/* transfer mode in use */
	int delay;			/* adapter delay setting */
	int devtype;			/* dummy for paride protocol modules */
	char *device;			/* dummy for paride protocol modules */
	int unit;			/* unit number for chained adapters */
	int saved_r0;			/* saved port state */
	int saved_r2;			/* saved port state */
	unsigned long private;		/* for protocol module */
	struct pardevice *pardev;	/* pointer to pardevice */
};

typedef struct pi_adapter PIA;	/* for paride protocol modules */

/* registers are addressed as (cont,regr)
 *	cont: 0 for command register file, 1 for control register(s)
 *	regr: 0-7 for register number.
 */

/* macros and functions exported to the protocol modules */
#define delay_p			(pi->delay ? udelay(pi->delay) : (void)0)
#define out_p(offs, byte)	do { outb(byte, pi->port + offs); delay_p; } while (0)
#define in_p(offs)		(delay_p, inb(pi->port + offs))

#define w0(byte)		out_p(0, byte)
#define r0()			in_p(0)
#define w1(byte)		out_p(1, byte)
#define r1()			in_p(1)
#define w2(byte)		out_p(2, byte)
#define r2()			in_p(2)
#define w3(byte)		out_p(3, byte)
#define w4(byte)		out_p(4, byte)
#define r4()			in_p(4)
#define w4w(data)		do { outw(data, pi->port + 4); delay_p; } while (0)
#define w4l(data)		do { outl(data, pi->port + 4); delay_p; } while (0)
#define r4w()			(delay_p, inw(pi->port + 4))
#define r4l()			(delay_p, inl(pi->port + 4))

static inline u16 pi_swab16(char *b, int k)
{
	union { u16 u; char t[2]; } r;

	r.t[0] = b[2 * k + 1]; r.t[1] = b[2 * k];
	return r.u;
}

static inline u32 pi_swab32(char *b, int k)
{
	union { u32 u; char f[4]; } r;

	r.f[0] = b[4 * k + 1]; r.f[1] = b[4 * k];
	r.f[2] = b[4 * k + 3]; r.f[3] = b[4 * k + 2];
	return r.u;
}

struct pi_protocol {
	char name[8];

	int max_mode;
	int epp_first;		/* modes >= this use 8 ports */

	int default_delay;
	int max_units;		/* max chained units probed for */

	void (*write_regr)(struct pi_adapter *pi, int cont, int regr, int val);
	int (*read_regr)(struct pi_adapter *pi, int cont, int regr);
	void (*write_block)(struct pi_adapter *pi, char *buf, int count);
	void (*read_block)(struct pi_adapter *pi, char *buf, int count);

	void (*connect)(struct pi_adapter *pi);
	void (*disconnect)(struct pi_adapter *pi);

	int (*test_port)(struct pi_adapter *pi);
	int (*probe_unit)(struct pi_adapter *pi);
	int (*test_proto)(struct pi_adapter *pi, char *scratch, int verbose);
	void (*log_adapter)(struct pi_adapter *pi, char *scratch, int verbose);

	int (*init_proto)(struct pi_adapter *pi);
	void (*release_proto)(struct pi_adapter *pi);
	struct module *owner;
	struct device_driver driver;
	struct scsi_host_template sht;
};

#define PATA_PARPORT_SHT ATA_PIO_SHT

int pata_parport_register_driver(struct pi_protocol *pr);
void pata_parport_unregister_driver(struct pi_protocol *pr);
/* defines for old paride protocol modules */
#define paride_register pata_parport_register_driver
#define paride_unregister pata_parport_unregister_driver

#endif /* LINUX_PATA_PARPORT_H */
