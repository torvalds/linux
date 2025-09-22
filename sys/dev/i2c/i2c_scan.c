/*	$OpenBSD: i2c_scan.c,v 1.147 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 2005 Theo de Raadt <deraadt@openbsd.org>
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

/*
 * I2C bus scanning.  We apologize in advance for the massive overuse of 0x.
 */

#include "ipmi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#define _I2C_PRIVATE
#include <dev/i2c/i2cvar.h>

#undef I2C_DEBUG
#define I2C_VERBOSE

#define MAX_IGNORE 8
u_int8_t ignore_addrs[MAX_IGNORE];

struct iicprobelist {
	u_int8_t start, end;
};

/*
 * Addresses at which to probe for sensors.  Skip address 0x4f, since
 * probing it seems to crash at least one Sony VAIO laptop.  Only a
 * few chips can actually sit at that address, and vendors seem to
 * place those at other addresses, so this isn't a big loss.
 */
struct iicprobelist probe_addrs_sensor[] = {
	{ 0x18, 0x1f },
	{ 0x20, 0x2f },
	{ 0x48, 0x4e },
	{ 0, 0 }
};

/*
 * Addresses at which to probe for eeprom devices.
 */
struct iicprobelist probe_addrs_eeprom[] = {
	{ 0x50, 0x57 },
	{ 0, 0 }
};

char 	*iic_probe_sensor(struct device *, u_int8_t);
char	*iic_probe_eeprom(struct device *, u_int8_t);

#define PFLAG_SENSOR	1
static struct {
	struct iicprobelist *pl;
	char	*(*probe)(struct device *, u_int8_t);
	int	flags;
} probes[] = {
	{ probe_addrs_sensor, iic_probe_sensor, PFLAG_SENSOR },
	{ probe_addrs_eeprom, iic_probe_eeprom, 0 },
	{ NULL, NULL }
};

/*
 * Some Maxim 1617 clones MAY NOT even read cmd 0xfc!  When it is
 * read, they will power-on-reset.  Their default condition
 * (control register bit 0x80) therefore will be that they assert
 * /ALERT for the 5 potential errors that may occur.  One of those
 * errors is that the external temperature diode is missing.  This
 * is unfortunately a common choice of system designers, except
 * suddenly now we get a /ALERT, which may on some chipsets cause
 * us to receive an entirely unexpected SMI .. and then an NMI.
 *
 * As we probe each device, if we hit something which looks suspiciously
 * like it may potentially be a 1617 or clone, we immediately set this
 * variable to avoid reading that register offset.
 */
int	skip_fc;

static i2c_tag_t probe_ic;
static u_int8_t probe_addr;
static u_int8_t probe_val[256];

void		iicprobeinit(struct i2cbus_attach_args *, u_int8_t);
u_int8_t	iicprobenc(u_int8_t);
u_int8_t	iicprobe(u_int8_t);
u_int16_t	iicprobew(u_int8_t);
char		*lm75probe(void);
char		*adm1032cloneprobe(u_int8_t);
void		iic_dump(struct device *, u_int8_t, char *);

void
iicprobeinit(struct i2cbus_attach_args *iba, u_int8_t addr)
{
	probe_ic = iba->iba_tag;
	probe_addr = addr;
	memset(probe_val, 0xff, sizeof probe_val);
}

u_int8_t
iicprobenc(u_int8_t cmd)
{
	u_int8_t data;

	/*
	 * If we think we are talking to an evil Maxim 1617 or clone,
	 * avoid accessing this register because it is death.
	 */
	if (skip_fc && cmd == 0xfc)
		return (0xff);
	iic_acquire_bus(probe_ic, 0);
	if (iic_exec(probe_ic, I2C_OP_READ_WITH_STOP,
	    probe_addr, &cmd, sizeof cmd, &data, sizeof data, 0) != 0)
		data = 0xff;
	iic_release_bus(probe_ic, 0);
	return (data);
}

u_int16_t
iicprobew(u_int8_t cmd)
{
	u_int16_t data;

	/*
	 * If we think we are talking to an evil Maxim 1617 or clone,
	 * avoid accessing this register because it is death.
	 */
	if (skip_fc && cmd == 0xfc)
		return (0xffff);
	iic_acquire_bus(probe_ic, 0);
	if (iic_exec(probe_ic, I2C_OP_READ_WITH_STOP,
	    probe_addr, &cmd, sizeof cmd, &data, sizeof data, 0) != 0)
		data = 0xffff;
	iic_release_bus(probe_ic, 0);
	return betoh16(data);
}

u_int8_t
iicprobe(u_int8_t cmd)
{
	if (probe_val[cmd] != 0xff)
		return probe_val[cmd];
	probe_val[cmd] = iicprobenc(cmd);
	return (probe_val[cmd]);
}

#define LM75TEMP	0x00
#define LM75CONF	0x01
#define LM75Thyst	0x02
#define LM75Tos		0x03
#define LM77Tlow	0x04
#define LM77Thigh	0x05
#define LM75TMASK	0xff80	/* 9 bits in temperature registers */
#define LM77TMASK	0xfff8	/* 13 bits in temperature registers */

/*
 * The LM75/LM75A/LM77 family are very hard to detect.  Thus, we check
 * for all other possible chips first.  These chips do not have an
 * ID register.  They do have a few quirks though:
 * -  on the LM75 and LM77, registers 0x06 and 0x07 return whatever
 *    value was read before
 * -  the LM75 lacks registers 0x04 and 0x05, so those act as above
 * -  the LM75A returns 0xffff for registers 0x04, 0x05, 0x06 and 0x07
 * -  the chip registers loop every 8 registers
 * The downside is that we must read almost every register to guess
 * if this is an LM75, LM75A or LM77.
 */
char *
lm75probe(void)
{
	u_int16_t temp, thyst, tos, tlow, thigh, mask = LM75TMASK;
	u_int8_t conf;
	int i, echocount, ffffcount, score;
	int echoreg67, echoreg45, ffffreg67, ffffreg45;

	temp = iicprobew(LM75TEMP);

	/*
	 * Sometimes the other probes can upset the chip, if we get 0xffff
	 * the first time, try it once more.
	 */
	if (temp == 0xffff)
		temp = iicprobew(LM75TEMP);

	conf = iicprobenc(LM75CONF);
	thyst = iicprobew(LM75Thyst);
	tos = iicprobew(LM75Tos);

	/* totally bogus data */
	if (conf == 0xff && temp == 0xffff && thyst == 0xffff)
		return (NULL);

	temp &= mask;
	thyst &= mask;
	tos &= mask;

	/* All values the same?  Very unlikely */
	if (temp == thyst && thyst == tos)
		return (NULL);

#if notsure
	/* more register aliasing effects that indicate not a lm75 */
	if ((temp >> 8) == conf)
		return (NULL);
#endif

	/*
	 * LM77/LM75 registers 6, 7
	 * echo whatever was read just before them from reg 0, 1, or 2
	 *
	 * LM75A doesn't appear to do this, but does appear to reliably
	 * return 0xffff
	 */
	for (i = 6, echocount = 2, ffffcount = 0; i <= 7; i++) {
		if ((iicprobew(LM75TEMP) & mask) != (iicprobew(i) & mask) ||
		    (iicprobew(LM75Thyst) & mask) != (iicprobew(i) & mask) ||
		    (iicprobew(LM75Tos) & mask) != (iicprobew(i) & mask))
			echocount--;
		if (iicprobew(i) == 0xffff)
			ffffcount++;
	}

	/* Make sure either both registers echo, or neither does */
	if (echocount == 1 || ffffcount == 1)
		return (NULL);

	echoreg67 = (echocount == 0) ? 0 : 1;
	ffffreg67 = (ffffcount == 0) ? 0 : 1;

	/*
	 * LM75 has no registers 4 or 5, and they will act as echos too
	 *
	 * LM75A doesn't appear to do this either, but does appear to
	 * reliably return 0xffff
	 */
	for (i = 4, echocount = 2, ffffcount = 0; i <= 5; i++) {
		if ((iicprobew(LM75TEMP) & mask) != (iicprobew(i) & mask) ||
		    (iicprobew(LM75Thyst) & mask) != (iicprobew(i) & mask) ||
		    (iicprobew(LM75Tos) & mask) != (iicprobew(i) & mask))
			echocount--;
		if (iicprobew(i) == 0xffff)
			ffffcount++;
	}

	/* Make sure either both registers echo, or neither does */
	if (echocount == 1 || ffffcount == 1)
		return (NULL);

	echoreg45 = (echocount == 0) ? 0 : 1;
	ffffreg45 = (ffffcount == 0) ? 0 : 1;

	/*
	 * If we find that 4 and 5 are not echos, and don't return 0xffff
	 * then based on whether the echo test of registers 6 and 7
	 * succeeded or not, we may have an LM77
	 */
	if (echoreg45 == 0 && ffffreg45 == 0 && echoreg67 == 1) {
		mask = LM77TMASK;

		/* mask size changed, must re-read for the next checks */
		thyst = iicprobew(LM75Thyst) & mask;
		tos = iicprobew(LM75Tos) & mask;
		tlow = iicprobew(LM77Tlow) & mask;
		thigh = iicprobew(LM77Thigh) & mask;
	}

	/* a real LM75/LM75A/LM77 repeats its registers.... */
	for (i = 0x08; i <= 0xf8; i += 8) {
		if (conf != iicprobenc(LM75CONF + i) ||
		    thyst != (iicprobew(LM75Thyst + i) & mask) ||
		    tos != (iicprobew(LM75Tos + i) & mask))
			return (NULL);

		/*
		 * Check that the repeated registers 0x06 and 0x07 still
		 * either echo or return 0xffff
		 */
		if (echoreg67 == 1) {
			tos = iicprobew(LM75Tos) & mask;
			if (tos != (iicprobew(0x06 + i) & mask) ||
			    tos != (iicprobew(0x07 + i) & mask))
				return (NULL);
		} else if (ffffreg67 == 1)
			if (iicprobew(0x06 + i) != 0xffff ||
			    iicprobew(0x07 + i) != 0xffff)
				return (NULL);

		/*
		 * Check that the repeated registers 0x04 and 0x05 still
		 * either echo or return 0xffff. If they do neither, and
		 * registers 0x06 and 0x07 echo, then we will be probing
		 * for an LM77, so make sure those still repeat
		 */
		if (echoreg45 == 1) {
			tos = iicprobew(LM75Tos) & mask;
			if (tos != (iicprobew(LM77Tlow + i) & mask) ||
			    tos != (iicprobew(LM77Thigh + i) & mask))
				return (NULL);
		} else if (ffffreg45 == 1) {
			if (iicprobew(LM77Tlow + i) != 0xffff ||
			    iicprobew(LM77Thigh + i) != 0xffff)
				return (NULL);
		} else if (echoreg67 == 1)
			if (tlow != (iicprobew(LM77Tlow + i) & mask) ||
			    thigh != (iicprobew(LM77Thigh + i) & mask))
				return (NULL);
	}

	/*
	 * Given that we now know how the first eight registers behave and
	 * that this behaviour is consistently repeated, we can now use
	 * the following table:
	 *
	 * echoreg67 | echoreg45 | ffffreg67 | ffffreg45 | chip
	 * ----------+-----------+-----------+-----------+------
	 *     1     |     1     |     0     |     0     | LM75
	 *     1     |     0     |     0     |     0     | LM77
	 *     0     |     0     |     1     |     1     | LM75A
	 */

	/* Convert the various flags into a single score */
	score = (echoreg67 << 3) + (echoreg45 << 2) + (ffffreg67 << 1) +
	    ffffreg45;

	switch (score) {
	case 12:
		return ("lm75");
	case 8:
		return ("lm77");
	case 3:
		return ("lm75a");
	default:
#if defined(I2C_DEBUG)
		printf("lm75probe: unknown chip, scored %d\n", score);
#endif /* defined(I2C_DEBUG) */
		return (NULL);
	}
}

char *
adm1032cloneprobe(u_int8_t addr)
{
	if (addr == 0x18 || addr == 0x1a || addr == 0x29 ||
	    addr == 0x2b || addr == 0x4c || addr == 0x4e) {
		u_int8_t reg, val;
		int zero = 0, copy = 0;

		val = iicprobe(0x00);
		for (reg = 0x00; reg < 0x09; reg++) {
			if (iicprobe(reg) == 0xff)
				return (NULL);
			if (iicprobe(reg) == 0x00)
				zero++;
			if (val == iicprobe(reg))
				copy++;
		}
		if (zero > 6 || copy > 6)
			return (NULL);
		val = iicprobe(0x09);
		for (reg = 0x0a; reg < 0xfc; reg++) {
			if (iicprobe(reg) != val)
				return (NULL);
		}
		/* 0xfe may be Maxim, or some other vendor */
		if (iicprobe(0xfe) == 0x4d)
			return ("max1617");
		/*
		 * "xeontemp" is the name we choose for clone chips
		 * which have all sorts of buggy bus interactions, such
		 * as those we just probed.  Why?
		 * Intel is partly to blame for this situation.
		 */
		return ("xeontemp");
	}
	return (NULL);
}

void
iic_ignore_addr(u_int8_t addr)
{
	int i;

	for (i = 0; i < sizeof(ignore_addrs); i++)
		if (ignore_addrs[i] == 0) {
			ignore_addrs[i] = addr;
			return;
		}
}

#ifdef I2C_VERBOSE
void
iic_dump(struct device *dv, u_int8_t addr, char *name)
{
	static u_int8_t iicvalcnt[256];
	u_int8_t val, val2, max;
	int i, cnt = 0;

	/*
	 * Don't bother printing the most often repeated register
	 * value, since it is often weird devices that respond
	 * incorrectly, busted controller driver, or in the worst
	 * case, it in mosts cases, the value 0xff.
	 */
	bzero(iicvalcnt, sizeof iicvalcnt);
	val = iicprobe(0);
	iicvalcnt[val]++;
	for (i = 1; i <= 0xff; i++) {
		val2 = iicprobe(i);
		iicvalcnt[val2]++;
		if (val == val2)
			cnt++;
	}

	for (val = max = i = 0; i <= 0xff; i++)
		if (max < iicvalcnt[i]) {
			max = iicvalcnt[i];
			val = i;
		}

	if (cnt == 255)
		return;

	printf("%s: addr 0x%x", dv->dv_xname, addr);
	for (i = 0; i <= 0xff; i++) {
		if (iicprobe(i) != val)
			printf(" %02x=%02x", i, iicprobe(i));
	}
	printf(" words");
	for (i = 0; i < 8; i++)
		printf(" %02x=%04x", i, iicprobew(i));
	if (name)
		printf(": %s", name);
	printf("\n");
}
#endif /* I2C_VERBOSE */

char *
iic_probe_sensor(struct device *self, u_int8_t addr)
{
	char *name = NULL;

	skip_fc = 0;

	/*
	 * Many I2C/SMBus devices use register 0x3e as a vendor ID
	 * register.
	 */
	switch (iicprobe(0x3e)) {
	case 0x01:		/* National Semiconductor */
		/*
		 * Some newer National products use a vendor code at
		 * 0x3e of 0x01, and then 0x3f contains a product code
		 * But some older products are missing a product code,
		 * and contain who knows what in that register.  We assume
		 * that some employee was smart enough to keep the numbers
		 * unique.
		 */
		if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) == 0x73 || iicprobe(0x3f) == 0x72) &&
		    iicprobe(0x00) == 0x00)
			name = "lm93";	/* product 0x72 is the prototype */
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3f) == 0x68)
			name = "lm96000";	/* adt7460 compat? */
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) == 0x60 || iicprobe(0x3f) == 0x62))
			name = "lm85";		/* lm85C/B == adt7460 compat */
		else if ((addr & 0x7c) == 0x2c &&	/* addr 0b01011xx */
		    iicprobe(0x48) == addr &&
		    (iicprobe(0x3f) == 0x03 || iicprobe(0x3f) == 0x04) &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "lm81";
		break;
	case 0x02:		/* National Semiconductor? */
		if ((iicprobe(0x3f) & 0xfc) == 0x04)
			name = "lm87";		/* complete check */
		break;
	case 0x23:		/* Analog Devices? */
		if (iicprobe(0x48) == addr &&
		    (iicprobe(0x40) & 0x80) == 0x00 &&
		    (addr & 0x7c) == 0x2c)
			name = "adm9240";	/* lm87 clone */
		break;
	case 0x41:		/* Analog Devices */
		/*
		 * Newer chips have a valid 0x3d product number, while
		 * older ones sometimes encoded the product into the
		 * upper half of the "step register" at 0x3f.
		 */
		if ((addr == 0x2c || addr == 0x2e || addr == 0x2f) &&
		    iicprobe(0x3d) == 0x70)
			name = "adt7470";
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3d) == 0x76)
			name = "adt7476"; /* or adt7476a */
		else if (addr == 0x2e && iicprobe(0x3d) == 0x75)
			name = "adt7475";
		else if (iicprobe(0x3d) == 0x27 &&
		    (iicprobe(0x3f) == 0x60 || iicprobe(0x3f) == 0x6a))
			name = "adm1027";	/* or adt7463 */
		else if (iicprobe(0x3d) == 0x27 &&
		    (iicprobe(0x3f) == 0x62 || iicprobe(0x3f) == 0x6a))
			name = "adt7460";	/* complete check */
		else if ((addr == 0x2c || addr == 0x2e) &&
		    iicprobe(0x3d) == 0x62 && iicprobe(0x3f) == 0x04)
			name = "adt7462";
		else if (addr == 0x4c &&
		    iicprobe(0x3d) == 0x66 && iicprobe(0x3f) == 0x02)
			name = "adt7466";
		else if (addr == 0x2e &&
		    iicprobe(0x3d) == 0x68 && (iicprobe(0x3f) & 0xf0) == 0x70)
			name = "adt7467"; /* or adt7468 */
		else if (iicprobe(0x3d) == 0x33 && iicprobe(0x3f) == 0x02)
			name = "adm1033";
		else if (iicprobe(0x3d) == 0x34 && iicprobe(0x3f) == 0x02)
			name = "adm1034";
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3d) == 0x30 &&
		    (iicprobe(0x01) & 0x80) == 0x00 &&
		    (iicprobe(0x0d) & 0x70) == 0x00 &&
		    (iicprobe(0x0e) & 0x70) == 0x00)
			/*
			 * Revision 3 seems to be an adm1031 with
			 * remote diode 2 shorted.  Therefore we
			 * cannot assume the reserved/unused bits of
			 * register 0x03 and 0x06 are set to zero.
			 */
			name = "adm1030";	/* complete check */
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3d) == 0x31 &&
		    (iicprobe(0x01) & 0x80) == 0x00 &&
		    (iicprobe(0x0d) & 0x70) == 0x00 &&
		    (iicprobe(0x0e) & 0x70) == 0x00 &&
		    (iicprobe(0x0f) & 0x70) == 0x00)
			name = "adm1031";	/* complete check */
		else if ((addr & 0x7c) == 0x2c &&	/* addr 0b01011xx */
		    (iicprobe(0x3f) & 0xf0) == 0x20 &&
		    (iicprobe(0x40) & 0x80) == 0x00 &&
		    (iicprobe(0x41) & 0xc0) == 0x00 &&
		    (iicprobe(0x42) & 0xbc) == 0x00)
			name = "adm1025";	/* complete check */
		else if ((addr & 0x7c) == 0x2c &&	/* addr 0b01011xx */
		    (iicprobe(0x3f) & 0xf0) == 0x10 &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "adm1024";	/* complete check */
		else if ((iicprobe(0xff) & 0xf0) == 0x30)
			name = "adm1023";
		else if (addr == 0x2e &&
		    (iicprobe(0x3f) & 0xf0) == 0xd0 &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "adm1028";	/* adm1022 clone? */
		else if ((addr == 0x2c || addr == 0x2e || addr == 0x2f) &&
		    (iicprobe(0x3f) & 0xf0) == 0xc0 &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "adm1022";
		break;
	case 0x49:		/* Texas Instruments */
		if ((addr == 0x2c || addr == 0x2e || addr == 0x2f) &&
		    (iicprobe(0x3f) & 0xf0) == 0xc0 &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "thmc50";	/* adm1022 clone */
		break;
	case 0x55:		/* SMSC */
		if ((addr & 0x7c) == 0x2c &&		/* addr 0b01011xx */
		    iicprobe(0x3f) == 0x20 &&
		    (iicprobe(0x47) & 0x70) == 0x00 &&
		    (iicprobe(0x49) & 0xfe) == 0x80)
			name = "47m192";	/* adm1025 compat */
		break;
	case 0x5c:		/* SMSC */
		if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) == 0x69))
			name = "sch5027";
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) & 0xf0) == 0x60)
			name = "emc6d100";   /* emc6d101, emc6d102, emc6d103 */
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) & 0xf0) == 0x80)
			name = "sch5017";
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    (iicprobe(0x3f) & 0xf0) == 0xb0)
			name = "emc6w201";
		break;
	case 0x61:		/* Andigilog */
		if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3f) == 0x69 &&
		    iicprobe(0x22) >= 0xaf &&		/* Vdd */
		    (iicprobe(0x09) & 0xbf) == 0x00 && iicprobe(0x0f) == 0x00 &&
		    (iicprobe(0x40) & 0xf0) == 0x00)
			name = "asc7611";
		else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
		    iicprobe(0x3f) == 0x6c &&
		    iicprobe(0x22) >= 0xae)		/* Vdd */
			name = "asc7621";
		break;
	case 0xa1:		/* Philips */
		if ((iicprobe(0x3f) & 0xf0) == 0x20 &&
		    (iicprobe(0x40) & 0x80) == 0x00 &&
		    (iicprobe(0x41) & 0xc0) == 0x00 &&
		    (iicprobe(0x42) & 0xbc) == 0x00)
			name = "ne1619";	/* adm1025 compat */
		break;
	case 0xda:		/* Dallas Semiconductor */
		if (iicprobe(0x3f) == 0x01 && iicprobe(0x48) == addr &&
		    (iicprobe(0x40) & 0x80) == 0x00)
			name = "ds1780";	/* lm87 clones */
		break;
	}

	switch (iicprobe(0x4e)) {
	case 0x41:		/* Analog Devices */
		if ((addr == 0x48 || addr == 0x4a || addr == 0x4b) &&
		    (iicprobe(0x4d) == 0x03 || iicprobe(0x4d) == 0x08 ||
		    iicprobe(0x4d) == 0x07))
			name = "adt7516";	/* adt7517, adt7519 */
		break;
	}

	switch (iicprobe(0xfe)) {
	case 0x01:		/* National Semiconductor */
		if (addr == 0x4c &&
		    iicprobe(0xff) == 0x41 && (iicprobe(0x03) & 0x18) == 0 &&
		    iicprobe(0x04) <= 0x0f && (iicprobe(0xbf) & 0xf8) == 0)
			name = "lm63";
		else if (addr == 0x4c &&
		    iicprobe(0xff) == 0x11 && (iicprobe(0x03) & 0x2a) == 0 &&
		    iicprobe(0x04) <= 0x09 && (iicprobe(0xbf) & 0xf8) == 0)
			name = "lm86";
		else if (addr == 0x4c &&
		    iicprobe(0xff) == 0x31 && (iicprobe(0x03) & 0x2a) == 0 &&
		    iicprobe(0x04) <= 0x09 && (iicprobe(0xbf) & 0xf8) == 0)
			name = "lm89";		/* or lm99 */
		else if (addr == 0x4d &&
		    iicprobe(0xff) == 0x34 && (iicprobe(0x03) & 0x2a) == 0 &&
		    iicprobe(0x04) <= 0x09 && (iicprobe(0xbf) & 0xf8) == 0)
			name = "lm89-1";	/* or lm99-1 */
		else if (addr == 0x4c &&
		    iicprobe(0xff) == 0x21 && (iicprobe(0x03) & 0x2a) == 0 &&
		    iicprobe(0x04) <= 0x09 && (iicprobe(0xbf) & 0xf8) == 0)
			name = "lm90";
		break;
	case 0x23:		/* Genesys Logic? */
		if ((addr == 0x4c) &&
		    (iicprobe(0x03) & 0x3f) == 0x00 && iicprobe(0x04) <= 0x08)
			/*
			 * Genesys Logic doesn't make the datasheet
			 * for the GL523SM publicly available, so
			 * the checks above are nothing more than a
			 * (conservative) educated guess.
			 */
			name = "gl523sm";
		break;
	case 0x41:		/* Analog Devices */
		if ((addr == 0x4c || addr == 0x4d) &&
		    iicprobe(0xff) == 0x51 &&
		    (iicprobe(0x03) & 0x1f) == 0x04 &&
		    iicprobe(0x04) <= 0x0a) {
			/* If not in adm1032 compatibility mode. */
			name = "adt7461";
		} else if ((addr == 0x18 || addr == 0x19 || addr == 0x1a ||
		    addr == 0x29 || addr == 0x2a || addr == 0x2b ||
		    addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    (iicprobe(0xff) & 0xf0) == 0x00 &&
		    (iicprobe(0x03) & 0x3f) == 0x00 &&
		    iicprobe(0x04) <= 0x07) {
			name = "adm1021";
			skip_fc = 1;
		} else if ((addr == 0x18 || addr == 0x19 || addr == 0x1a ||
		    addr == 0x29 || addr == 0x2a || addr == 0x2b ||
		    addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    (iicprobe(0xff) & 0xf0) == 0x30 &&
		    (iicprobe(0x03) & 0x3f) == 0x00 &&
		    iicprobe(0x04) <= 0x07) {
			name = "adm1023";	/* or adm1021a */
			skip_fc = 1;
		} else if ((addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    (iicprobe(0x03) & 0x3f) == 0x00 &&
		    iicprobe(0x04) <= 0x0a) {
			name = "adm1032";	/* or adm1020 */
			skip_fc = 1;
		}
		break;
	case 0x47:		/* Global Mixed-mode Technology */
		if (addr == 0x4c && iicprobe(0xff) == 0x01 &&
		    (iicprobe(0x03) & 0x3f) == 0x00 && iicprobe(0x04) <= 0x08)
			name = "g781";
		if (addr == 0x4d && iicprobe(0xff) == 0x03 &&
		    (iicprobe(0x03) & 0x3f) == 0x00 && iicprobe(0x04) <= 0x08)
			name = "g781-1";
		break;
	case 0x4d:		/* Maxim */
		if ((addr == 0x18 || addr == 0x19 || addr == 0x1a ||
		     addr == 0x29 || addr == 0x2a || addr == 0x2b ||
		     addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    iicprobe(0xff) == 0x08 && (iicprobe(0x02) & 0x03) == 0 &&
		    (iicprobe(0x03) & 0x07) == 0 && iicprobe(0x04) <= 0x08)
			name = "max6690";
		else if ((addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    iicprobe(0xff) == 0x59 && (iicprobe(0x03) & 0x1f) == 0 &&
		    iicprobe(0x04) <= 0x07)
			name = "max6646";	/* max6647/8/9, max6692 */
		else if ((addr == 0x4c || addr == 0x4d || addr == 0x4e) &&
		    (iicprobe(0x02) & 0x2b) == 0 &&
		    (iicprobe(0x03) & 0x0f) == 0 && iicprobe(0x04) <= 0x09) {
			name = "max6657";	/* max6658, max6659 */
			skip_fc = 1;
		} else if ((addr >= 0x48 && addr <= 0x4f) &&
		    (iicprobe(0x02) & 0x2b) == 0 &&
		    (iicprobe(0x03) & 0x0f) == 0)
			name = "max6642";
		break;
	case 0x55:		/* Texas Instruments */
		if (addr == 0x4c && iicprobe(0xff) == 0x11 &&
		    (iicprobe(0x03) & 0x1b) == 0x00 &&
		    (iicprobe(0x04) & 0xf0) == 0x00 &&
		    (iicprobe(0x10) & 0x0f) == 0x00 &&
		    (iicprobe(0x13) & 0x0f) == 0x00 &&
		    (iicprobe(0x14) & 0x0f) == 0x00 &&
		    (iicprobe(0x15) & 0x0f) == 0x00 &&
		    (iicprobe(0x16) & 0x0f) == 0x00 &&
		    (iicprobe(0x17) & 0x0f) == 0x00)
			name = "tmp401";
		break;
	case 0xa1:
		if ((addr >= 0x48 && addr <= 0x4f) &&
		    iicprobe(0xff) == 0x00 &&
		    (iicprobe(0x03) & 0xf8) == 0x00 &&
		    iicprobe(0x04) <= 0x09) {
			name = "sa56004x";	/* NXP sa56004x */
			skip_fc = 1;
		}
		break;
	}

	if (addr == iicprobe(0x48) &&
	    ((iicprobe(0x4f) == 0x5c && (iicprobe(0x4e) & 0x80)) ||
	    (iicprobe(0x4f) == 0xa3 && !(iicprobe(0x4e) & 0x80)))) {
		/*
		 * We could toggle 0x4e bit 0x80, then re-read 0x4f to
		 * see if the value changes to 0xa3 (indicating Winbond).
		 * But we are trying to avoid writes.
		 */
		if ((iicprobe(0x4e) & 0x07) == 0) {
			switch (iicprobe(0x58)) {
			case 0x10:
			case 0x11:			/* rev 2? */
				name = "w83781d";
				break;
			case 0x21:
				name = "w83627hf";
				break;
			case 0x30:
				name = "w83782d";
				break;
			case 0x31:
				name = "as99127f";	/* rev 2 */
				break;
			case 0x40:
				name = "w83783s";
				break;
			case 0x71:
				name = "w83791d";
				break;
			case 0x72:
				name = "w83791sd";
				break;
			case 0x7a:
				name = "w83792d";
				break;
			case 0xc1:
				name = "w83627dhg";
				break;
			}
		} else {
			/*
			 * The BIOS left the chip in a non-zero
			 * register bank.  Assume it's a W83781D and
			 * let lm(4) sort out the real model.
			 */
			name = "w83781d";
		}
	} else if (addr == (iicprobe(0xfc) & 0x7f) &&
	    iicprobe(0xfe) == 0x79 && iicprobe(0xfb) == 0x51 &&
	    ((iicprobe(0xfd) == 0x5c && (iicprobe(0x00) & 0x80)) ||
	    (iicprobe(0xfd) == 0xa3 && !(iicprobe(0x00) & 0x80)))) {
		/*
		 * We could toggle 0x00 bit 0x80, then re-read 0xfd to
		 * see if the value changes to 0xa3 (indicating Nuvoton).
		 * But we are trying to avoid writes.
		 */
		name = "w83795g";
	} else if (addr == iicprobe(0x4a) && iicprobe(0x4e) == 0x50 &&
	    iicprobe(0x4c) == 0xa3 && iicprobe(0x4d) == 0x5c) {
		name = "w83l784r";
	} else if (addr == 0x2d && iicprobe(0x4e) == 0x60 &&
	    iicprobe(0x4c) == 0xa3 && iicprobe(0x4d) == 0x5c) {
		name = "w83l785r";
	} else if (addr == 0x2e && iicprobe(0x4e) == 0x70 &&
	    iicprobe(0x4c) == 0xa3 && iicprobe(0x4d) == 0x5c) {
		name = "w83l785ts-l";
	} else if (addr >= 0x2c && addr <= 0x2f &&
	    ((iicprobe(0x00) & 0x07) != 0x0 ||
	    ((iicprobe(0x00) & 0x07) == 0x0 && addr * 2 == iicprobe(0x0b) &&
	    (iicprobe(0x0c) & 0x40) && !(iicprobe(0x0c) & 0x04))) &&
	    iicprobe(0x0e) == 0x7b &&
	    (iicprobe(0x0f) & 0xf0) == 0x10 &&
	    ((iicprobe(0x0d) == 0x5c && (iicprobe(0x00) & 0x80)) ||
	    (iicprobe(0x0d) == 0xa3 && !(iicprobe(0x00) & 0x80)))) {
		name = "w83793g";
	} else if (addr >= 0x28 && addr <= 0x2f &&
	    iicprobe(0x4f) == 0x12 && (iicprobe(0x4e) & 0x80)) {
		/*
		 * We could toggle 0x4e bit 0x80, then re-read 0x4f to
		 * see if the value changes to 0xc3 (indicating ASUS).
		 * But we are trying to avoid writes.
		 */
		if (iicprobe(0x58) == 0x31)
			name = "as99127f";	/* rev 1 */
	} else if ((addr == 0x2d || addr == 0x2e) &&
	    addr * 2 == iicprobe(0x04) &&
	    iicprobe(0x5d) == 0x19 && iicprobe(0x5e) == 0x34 &&
	    iicprobe(0x5a) == 0x03 && iicprobe(0x5b) == 0x06) {
		name = "f75375";	/* Fintek */
	} else if (addr == 0x2d &&
	    ((iicprobe(0x4f) == 0x06 && (iicprobe(0x4e) & 0x80)) ||
	    (iicprobe(0x4f) == 0x94 && !(iicprobe(0x4e) & 0x80)))) {
		/*
		 * We could toggle 0x4e bit 0x80, then re-read 0x4f to
		 * see if the value changes to 0x94 (indicating ASUS).
		 * But we are trying to avoid writes.
		 *
		 * NB. we won't match if the BIOS has selected a non-zero
		 * register bank (set via 0x4e). We could select bank 0 so
		 * we see the right registers, but that would require a
		 * write.  In general though, we bet no BIOS would leave us
		 * in the wrong state.
		 */
		if ((iicprobe(0x58) & 0x7f) == 0x31 &&
		    (iicprobe(0x4e) & 0xf) == 0x00)
			name = "asb100";
	} else if ((addr == 0x2c || addr == 0x2d) &&
	    iicprobe(0x00) == 0x80 &&
	    (iicprobe(0x01) == 0x00 || iicprobe(0x01) == 0x80) &&
	    iicprobe(0x02) == 0x00 && (iicprobe(0x03) & 0x83) == 0x00 &&
	    (iicprobe(0x0f) & 0x07) == 0x00 &&
	    (iicprobe(0x11) & 0x80) == 0x00 &&
	    (iicprobe(0x12) & 0x80) == 0x00) {
		/*
		 * The GL518SM is really crappy.  It has both byte and
		 * word registers, and reading a word register with a
		 * byte read command will make the device crap out and
		 * hang the bus.  This has nasty consequences on some
		 * machines, like preventing warm reboots.  The word
		 * registers are 0x07 through 0x0c, so make sure the
		 * checks above don't access those registers.  We
		 * don't want to do this check right up front though
		 * since this chip is somewhat hard to detect (which
		 * is why we check for every single fixed bit it has).
		 */
		name = "gl518sm";
	} else if ((addr == 0x2c || addr == 0x2d || addr == 0x2e) &&
	    iicprobe(0x16) == 0x41 && ((iicprobe(0x17) & 0xf0) == 0x40)) {
		name = "adm1026";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1131 &&
	    (iicprobew(0x07) & 0xfffc) == 0xa200) {
		name = "se97";		/* or se97b */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1131 &&
	    (iicprobew(0x07) & 0xfffc) == 0xa100 &&
	    (iicprobew(0x00) & 0xfff0) == 0x0010) {
		name = "se98";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x004d &&
	    iicprobew(0x07) == 0x3e00 &&
	    (iicprobew(0x00) & 0xffe0) == 0x0000) {
		name = "max6604";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x0054 &&
	    (iicprobew(0x07) & 0xfffc) == 0x0200 &&
	    (iicprobew(0x00) & 0xffe0) == 0x0000) {
		name = "mcp9804";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x0054 &&
	    (iicprobew(0x07) & 0xff00) == 0x0000 &&
	    (iicprobew(0x00) & 0xffe0) == 0x0000) {
		name = "mcp9805";		/* or mcp9843 */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x0054 &&
	    (iicprobew(0x07) & 0xfffc) == 0x2000 &&
	    (iicprobew(0x00) & 0xffe0) == 0x0000) {
		name = "mcp98242";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x0054 &&
	    (iicprobew(0x07) & 0xff00) == 0x2100 &&
	    (iicprobew(0x00) & 0xff00) == 0x0000) {
		name = "mcp98243";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x0054 &&
	    (iicprobew(0x07) & 0xfffc) == 0x2200 &&
	    (iicprobew(0x00) & 0xff00) == 0x0000) {
		name = "mcp98244";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x11d4 &&
	    iicprobew(0x07) == 0x0800 &&
	    iicprobew(0x00) == 0x001d) {
		name = "adt7408";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x104a &&
	    (iicprobew(0x07) & 0xfffe) == 0x0000 &&
	    (iicprobew(0x00) == 0x002d || iicprobew(0x00) == 0x002f)) {
		name = "stts424e02";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x104a &&
	    (iicprobew(0x07) & 0xfffe) == 0x0300 &&
	    (iicprobew(0x00) == 0x006f)) {
		name = "stts2002";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x104a &&
	    (iicprobew(0x07) & 0xffff) == 0x2201 &&
	    (iicprobew(0x00) == 0x00ef)) {
		name = "stts2004";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x104a &&
	    (iicprobew(0x07) & 0xffff) == 0x0200 &&
	    (iicprobew(0x00) == 0x006f)) {
		name = "stts3000";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x104a &&
	    (iicprobew(0x07) & 0xffff) == 0x0101 &&
	    (iicprobew(0x00) == 0x002d || iicprobew(0x00) == 0x002f)) {
		name = "stts424";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1b09 &&
	    (iicprobew(0x07) & 0xffe0) == 0x0800 &&
	    (iicprobew(0x00) & 0x001f) == 0x001f) {
		name = "cat34ts02";		/* or cat6095, prod 0x0813 */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1b09 &&
	    (iicprobew(0x07) & 0xffff) == 0x0a00 &&
	    (iicprobew(0x00) & 0x001f) == 0x001f) {
		name = "cat34ts02c";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1b09 &&
	    (iicprobew(0x07) & 0xffff) == 0x2200 &&
	    (iicprobew(0x00) == 0x007f)) {
		name = "cat34ts04";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x00b3 &&
	    (iicprobew(0x07) & 0xffff) == 0x2903 &&
	    (iicprobew(0x00) == 0x004f)) {
		name = "ts3000b3";		/* or tse2002b3 */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x00b3 &&
	    (iicprobew(0x07) & 0xffff) == 0x2912 &&
	    (iicprobew(0x00) == 0x006f)) {
		name = "ts3000gb2";		/* or tse2002gb2 */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x00b3 &&
	    (iicprobew(0x07) & 0xffff) == 0x2913 &&
	    (iicprobew(0x00) == 0x0077)) {
		name = "ts3000gb0";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x00b3 &&
	    (iicprobew(0x07) & 0xffff) == 0x3001 &&
	    (iicprobew(0x00) == 0x006f)) {
		name = "ts3001gb2";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x00b3 &&
	    (iicprobew(0x07) & 0xffff) == 0x2214 &&
	    (iicprobew(0x00) == 0x00ff)) {
		name = "tse2004gb2";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x001f &&
	    (iicprobew(0x07) & 0xffff) == 0x8201 &&
	    (iicprobew(0x00) & 0xff00) == 0x0000) {
		name = "at30ts00";		/* or at30tse002 */
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1114 &&
	    (iicprobew(0x07) & 0xffff) == 0x2200 &&
	    (iicprobew(0x00) & 0xff00) == 0x0000) {
		name = "at30tse004";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x1c68 &&
	    (iicprobew(0x07) & 0xffff) == 0x2201 &&
	    (iicprobew(0x00) & 0xff00) == 0x0000) {
		name = "gt30ts00";
	} else if ((addr & 0x78) == 0x18 && iicprobew(0x06) == 0x132d &&
	    (iicprobew(0x07) & 0xffff) == 0x3300 &&
	    (iicprobew(0x00) & 0x001f) == 0x001f) {
		name = "gt34ts02";
	} else if ((addr & 0x7e) == 0x1c && iicprobe(0x0f) == 0x3b &&
	    (iicprobe(0x21) & 0x60) == 0x00 &&
	    iicprobe(0x0f) == iicprobe(0x8f) &&	/* registers address is 7 bits */
	    iicprobe(0x20) == iicprobe(0xa0) &&
	    iicprobe(0x21) == iicprobe(0xa1) &&
	    iicprobe(0x22) == iicprobe(0xa2) &&
	    iicprobe(0x07) == 0x00) {		/* 0x00 to 0x0e are reserved */
		name = "lis331dl";
	} else if (name == NULL &&
	    (addr & 0x78) == 0x48) {		/* addr 0b1001xxx */
		name = lm75probe();
	}
#if 0
	/*
	 * XXX This probe needs to be improved; the driver does some
	 * dangerous writes.
	 */
	if (name == NULL && (addr & 0x7c) == 0x48 &&	/* addr 0b1001xxx */
	    (iicprobew(0xaa) & 0x0007) == 0x0000 &&
	    (iicprobew(0xa1) & 0x0007) == 0x0000 &&
	    (iicprobew(0xa2) & 0x0007) == 0x0000 &&
	    (iicprobe(0xac) & 0x10) == 0x00) {
		if ((iicprobe(0xac) & 0x7e) == 0x0a &&
		    iicprobe(0xab) == 0x00 && iicprobe(0xa8) == 0x00)
			name = "ds1624";
		else if ((iicprobe(0xac) & 0x7e) == 0x0c)
			name = "ds1631";	/* terrible probe */
		else if ((iicprobe(0xac) & 0x2e) == 0x2e)
			name = "ds1721";	/* terrible probe */
	}
#endif
	if (name == NULL && (addr & 0xf8) == 0x28 && iicprobe(0x48) == addr &&
	    (iicprobe(0x00) & 0x90) == 0x10 && iicprobe(0x58) == 0x90) {
		if (iicprobe(0x5b) == 0x12)
			name = "it8712";
		else if (iicprobe(0x5b) == 0x00)
			name = "it8712f-a";		/* sis950 too */
	}

	if (name == NULL && iicprobe(0x48) == addr &&
	    (iicprobe(0x40) & 0x80) == 0x00 && iicprobe(0x58) == 0xac)
		name = "mtp008";

	if (name == NULL) {
		name = adm1032cloneprobe(addr);
		if (name)
			skip_fc = 1;
	}

	return (name);
}

char *
iic_probe_eeprom(struct device *self, u_int8_t addr)
{
	u_int8_t type;
	char *name = NULL;

	type = iicprobe(0x02);
	/* limit to SPD types seen in the wild */
	if (type < 4 || type > 16)
		return (name);

	/* more matching in driver(s) */
	name = "eeprom";

	return (name);
}

void
iic_scan(struct device *self, struct i2cbus_attach_args *iba)
{
	i2c_tag_t ic = iba->iba_tag;
	struct i2c_attach_args ia;
	struct iicprobelist *pl;
	u_int8_t cmd = 0, addr;
	char *name;
	int i, j, k;

	bzero(ignore_addrs, sizeof(ignore_addrs));

	for (i = 0; probes[i].probe; i++) {
#if NIPMI > 0
		extern int ipmi_enabled;

		if ((probes[i].flags & PFLAG_SENSOR) && ipmi_enabled) {
			printf("%s: skipping sensors to avoid ipmi0 interactions\n",
			    self->dv_xname);
			continue;
		}
#endif
		pl = probes[i].pl;
		for (j = 0; pl[j].start && pl[j].end; j++) {
			for (addr = pl[j].start; addr <= pl[j].end; addr++) {
				for (k = 0; k < sizeof(ignore_addrs); k++)
					if (ignore_addrs[k] == addr)
						break;
				if (k < sizeof(ignore_addrs))
					continue;

				/* Perform RECEIVE BYTE command */
				iic_acquire_bus(ic, 0);
				if (iic_exec(ic, I2C_OP_READ_WITH_STOP, addr,
				    &cmd, sizeof cmd, NULL, 0, 0) == 0) {
					iic_release_bus(ic, 0);

					/* Some device exists */
					iicprobeinit(iba, addr);
					name = (*probes[i].probe)(self, addr);
#ifndef I2C_VERBOSE
					if (name == NULL)
						name = "unknown";
#endif /* !I2C_VERBOSE */
					if (name) {
						memset(&ia, 0, sizeof(ia));
						ia.ia_tag = iba->iba_tag;
						ia.ia_addr = addr;
						ia.ia_size = 1;
						ia.ia_name = name;
						if (config_found(self,
						    &ia, iic_print))
							continue;
					}
#ifdef I2C_VERBOSE
					if ((probes[i].flags & PFLAG_SENSOR))
						iic_dump(self, addr, name);
#endif /* I2C_VERBOSE */
				} else
					iic_release_bus(ic, 0);
			}
		}
	}
}
