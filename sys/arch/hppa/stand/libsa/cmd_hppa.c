/*	$OpenBSD: cmd_hppa.c,v 1.12 2010/12/06 22:51:45 jasper Exp $	*/

/*
 * Copyright (c) 2002, 2009 Miodrag Vallat
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
/* would come from <sys/param.h> if -D_KERNEL */
#define offsetof(s, e) ((size_t)&((s *)0)->e)

#include <machine/iomod.h>
#include <machine/pdc.h>

#include <arch/hppa/dev/cpudevs.h>
#include <arch/hppa/dev/elroyreg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <libsa.h>
#include "cmd.h"
#include "dev_hppa.h"	/* pdc */

extern struct stable_storage sstor;
extern int sstorsiz;

/* storage sizes we're interested in */
#define	CONSOLEOFFSET \
	offsetof(struct stable_storage, ss_console)
#define	CONSOLESIZE \
	(offsetof(struct stable_storage, ss_console) + \
	 sizeof(struct device_path))

#define	KEYBOARDOFFSET \
	offsetof(struct stable_storage, ss_keyboard)
#define	KEYBOARDSIZE \
	(offsetof(struct stable_storage, ss_keyboard) + \
	 sizeof(struct device_path))

/*
 * Table for the possible console devices found during the device walk.
 */
struct consoledev {
	struct device_path dp;
	int	type;
	int	iodc_type;
	int	iodc_model;
};

#define	PS2		1
#define	HIL		2
#define	USB		3
#define	SERIAL		4
#define	GRAPHICS	5

#define	MAX_SERIALS	4
#define	MAX_KEYBOARDS	4
#define	MAX_GRAPHICS	4

struct consoledev serials[MAX_SERIALS];
struct consoledev keyboards[MAX_KEYBOARDS];
struct consoledev graphics[MAX_GRAPHICS];

int walked;

void	bus_walk(struct device_path *, int);
uint32_t dino_conf_read(u_int, int, int, u_int);
uint32_t elroy_conf_read(u_int, int, int, u_int);
int	path_match(struct device_path *, struct device_path *);
void	path_shift(struct device_path *, int);
void	pci_bus_walk(struct device_path *, struct iodc_data *,
	    struct pdc_memmap *);
void	register_device(struct consoledev *, int, struct device_path *,
	    struct iodc_data *, int, int);

int	Xconsole(void);
void	print_console(void);
int	set_graphics(struct device_path *, int, char *);
int	set_serial(struct device_path *, int, char *);
int	set_console(struct device_path *);

int	Xkeyboard(void);
void	print_keyboard(void);
int	set_keyboard(struct device_path *);

struct cmd_table cmd_machine[] = {
	{ "console",	CMDT_CMD,	Xconsole },
	{ "keyboard",	CMDT_CMD,	Xkeyboard },
	{ NULL, },
};

/* value to console speed table */
const int i_speeds[] = {
	50,
	75,
	110,
	150,
	300,
	600,
	1200,
	2400,
	4800,
	7200,
	9600,
	19200,
	38400,
	57600,
	115200,
	230400,
};

const char *c_speeds[] = {
	"50",
	"75",
	"110",
	"150",
	"300",
	"600",
	"1200",
	"2400",
	"4800",
	"7200",
	"9600",
	"19200",
	"38400",
	"57600",
	"115200",
	"230400",
};

/* values to console parity table */
const char *parities[] = {
	"none",
	"odd",
	"<unknown parity>",
	"even",
};

/*
 * C O N S O L E   S E T T I N G S
 */

void
print_console()
{
	int port, mode, speed, parity, bits;
	int i;

#ifdef DEBUG
	printf("console flags %x mod %x bc %d/%d/%d/%d/%d/%d\n",
	    sstor.ss_console.dp_flags,
	    sstor.ss_console.dp_mod,
	    sstor.ss_console.dp_bc[0],
	    sstor.ss_console.dp_bc[1],
	    sstor.ss_console.dp_bc[2],
	    sstor.ss_console.dp_bc[3],
	    sstor.ss_console.dp_bc[4],
	    sstor.ss_console.dp_bc[5]);

	printf("console path %x/%x/%x/%x/%x/%x\n",
	    sstor.ss_console.dp_layers[0],
	    sstor.ss_console.dp_layers[1],
	    sstor.ss_console.dp_layers[2],
	    sstor.ss_console.dp_layers[3],
	    sstor.ss_console.dp_layers[4],
	    sstor.ss_console.dp_layers[5]);
#endif

	printf("Console path: ");

	/* look for a serial console */
	for (port = i = 0; i < MAX_SERIALS; i++)
		if (path_match(&serials[i].dp, &sstor.ss_console)) {
			port = i + 1;
			break;
		}

	if (port == 0) {
		/*
		 * Graphics console
		 */

		for (port = i = 0; i < MAX_GRAPHICS; i++)
			if (path_match(&graphics[i].dp, &sstor.ss_console)) {
				port = i;
				break;
			}

		/*
		 * If the console could still not be identified, consider
		 * it is a simplified encoding for the default graphics
		 * console. Hence port == 0, no need to check.
		 */
		if (port == 0)
			printf("graphics");
		else
			printf("graphics_%d", port);

		mode = sstor.ss_console.dp_layers[0];
		if (mode != 0)
			printf(".%d", mode);
	} else {
		/*
		 * Serial console
		 */

		if (port == 1)
			printf("rs232");
		else
			printf("rs232_%d", port);

		speed = PZL_SPEED(sstor.ss_console.dp_layers[0]);
		printf(".%d", i_speeds[speed]);

		bits = PZL_BITS(sstor.ss_console.dp_layers[0]);
		printf(".%d", bits);

		parity = PZL_PARITY(sstor.ss_console.dp_layers[0]);
		printf(".%s", parities[parity]);
	}

	printf("\n");
}

int
set_graphics(console, port, arg)
	struct device_path *console;
	int port;
	char *arg;
{
	int maxmode, mode = 0;
	char *digit;

	/* head */
	if (graphics[port].type == 0) {
		printf("no such device found\n");
		return 0;
	}

	/* mode */
	if (arg != NULL) {
		for (digit = arg; *digit != '\0'; digit++) {
			if (*digit >= '0' && *digit <= '9')
				mode = 10 * mode + (*digit - '0');
			else {
				printf("invalid mode specification, %s\n", arg);
				return 0;
			}
		}

		if (mode <= 0) {
			printf("invalid mode specification, %s\n", arg);
			return 0;
		}
	}

	/*
	 * If we are just changing the mode of the same graphics
	 * console, check that our mode is in the valid range.
	 */
	if (path_match(&graphics[port].dp, &sstor.ss_console)) {
		maxmode = sstor.ss_console.dp_layers[1];

		/* pick back same mode if unspecified */
		if (mode == 0)
			mode = sstor.ss_console.dp_layers[0];

		if (mode > maxmode) {
			printf("invalid mode value, available range is 1-%d\n",
			    maxmode);
			return 0;
		}
	} else {
		if (mode == 0)
			mode = 1;
		maxmode = mode;
	}

	*console = graphics[port].dp;
	console->dp_layers[0] = mode;
	console->dp_layers[1] = maxmode;
	console->dp_layers[2] = console->dp_layers[3] =
	console->dp_layers[4] = console->dp_layers[5] = 0;

	return 1;
}

int
set_serial(console, port, arg)
	struct device_path *console;
	int port;
	char *arg;
{
	char *dot;
	int i;
	int speed, parity, bits;

	/* port */
	port--;
	if (serials[port].type == 0) {
		printf("no such device found\n");
		return 0;
	}

	/* speed */
	dot = strchr(arg, '.');
	if (dot != NULL)
		*dot++ = '\0';

	speed = 0;
	if (arg == NULL || *arg == '\0') {
		for (i = 0; i < nitems(i_speeds); i++)
			if (i_speeds[i] == 9600) {
				speed = i;
				break;
			}
	} else {
		for (i = 0; i < nitems(c_speeds); i++)
			if (strcmp(arg, c_speeds[i]) == 0) {
				speed = i;
				break;
			}
		if (speed == 0) {
			printf("invalid speed specification, %s\n", arg);
			return 0;
		}
	}

	/* data bits */
	arg = dot;
	dot = strchr(arg, '.');

	if (arg == NULL || *arg == '\0')
		bits = 8;
	else {
		if (dot == arg + 1)
			bits = *arg - '0';
		else
			bits = 0;

		if (bits < 5 || bits > 8) {
			printf("invalid bits specification, %s\n", arg);
			return 0;
		}
	}
	if (dot != NULL)
		*dot++ = '\0';

	/* parity */
	arg = dot;
	if (arg == NULL || *arg == '\0')
		parity = 0;	/* none */
	else {
		parity = -1;
		for (i = 0; i <= 3; i++)
			if (strcmp(arg, parities[i]) == 0) {
				parity = i;
				break;
			}
		if (parity == 2)
			parity = -1;	/* unknown parity */
	}
	if (parity < 0) {
		printf("invalid parity specification, %s\n", arg);
		return 0;
	}

	*console = serials[port].dp;
	console->dp_layers[0] = PZL_ENCODE(bits, parity, speed);

	return 1;
}

int
set_console(console)
	struct device_path *console;
{
	char *arg = cmd.argv[1], *dot;
	int port;

	/* extract first word */
	dot = strchr(arg, '.');
	if (dot != NULL)
		*dot++ = '\0';

	/*
	 * Graphics console
	 */
	if (strcmp(arg, "graphics") == 0)
		return set_graphics(console, 0, dot);
	if (strncmp(arg, "graphics_", 9) == 0) {
		port = arg[9] - '0';
		if (port > 0 && port < MAX_GRAPHICS)
			return set_graphics(console, port, dot);
	}

	/*
	 * Serial console
	 */
	if (strcmp(arg, "rs232") == 0)
		return set_serial(console, 1, dot);
	if (strncmp(arg, "rs232_", 6) == 0) {
		port = arg[6] - '0';
		if (port > 0 && port <= MAX_SERIALS)
			return set_serial(console, port, dot);
	}

	printf("invalid device specification, %s\n", arg);
	return 0;
}

int
Xconsole()
{
	struct device_path console;
	int rc;

	/* walk the device list if not already done */
	if (walked == 0) {
		bus_walk(NULL, MAXMODBUS);
		walked++;
	}

	if (sstorsiz < CONSOLESIZE) {
		printf("no console information in stable storage\n");
		return 0;
	}

	if (cmd.argc == 1) {
		print_console();
	} else {
		console = sstor.ss_console;
		if (set_console(&console)) {
			if (memcmp(&sstor.ss_console, &console,
			    sizeof console) != 0) {
				sstor.ss_console = console;

				/* alea jacta est */
				rc = (*pdc)(PDC_STABLE, PDC_STABLE_WRITE,
				    CONSOLEOFFSET, &sstor.ss_console,
				    sizeof(sstor.ss_console));
				if (rc != 0) {
					printf("failed to save console"
					    " settings, error %d\n", rc);
					/* read sstor again for safety */
					(*pdc)(PDC_STABLE, PDC_STABLE_READ,
					    CONSOLEOFFSET, &sstor.ss_console,
					    sizeof(sstor.ss_console));
				} else
					printf("you will need to power-cycle "
					       "your machine for the changes "
					       "to take effect.\n");
			}
			print_console();
		}
	}

	return 0;
}

/*
 * K E Y B O A R D   S E T T I N G S
 */

void
print_keyboard()
{
	int type;
	int i;

#ifdef DEBUG
	printf("keyboard flags %x mod %x bc %d/%d/%d/%d/%d/%d\n",
	    sstor.ss_keyboard.dp_flags,
	    sstor.ss_keyboard.dp_mod,
	    sstor.ss_keyboard.dp_bc[0],
	    sstor.ss_keyboard.dp_bc[1],
	    sstor.ss_keyboard.dp_bc[2],
	    sstor.ss_keyboard.dp_bc[3],
	    sstor.ss_keyboard.dp_bc[4],
	    sstor.ss_keyboard.dp_bc[5]);

	printf("keyboard path %x/%x/%x/%x/%x/%x\n",
	    sstor.ss_keyboard.dp_layers[0],
	    sstor.ss_keyboard.dp_layers[1],
	    sstor.ss_keyboard.dp_layers[2],
	    sstor.ss_keyboard.dp_layers[3],
	    sstor.ss_keyboard.dp_layers[4],
	    sstor.ss_keyboard.dp_layers[5]);
#endif

	printf("Keyboard path: ");

	for (type = i = 0; i < MAX_KEYBOARDS; i++)
		if (path_match(&keyboards[i].dp, &sstor.ss_keyboard)) {
			type = keyboards[i].type;
			break;
		}

	switch (type) {
	case USB:
		printf("usb");
		break;
	case HIL:
		printf("hil");
		break;
	case PS2:
		printf("ps2");
		break;
	default:
		printf("unknown");
		break;
	}

	printf("\n");
}

int
set_keyboard(keyboard)
	struct device_path *keyboard;
{
	int i;
	char *arg = cmd.argv[1];
	int type;

	if (strcmp(arg, "hil") == 0)
		type = HIL;
	else if (strcmp(arg, "ps2") == 0)
		type = PS2;
	else if (strcmp(arg, "usb") == 0)
		type = USB;
	else {
		/* XXX should probably handle multiple USB controllers */
		printf("invalid device specification, %s\n", arg);
		return 0;
	}

	for (i = 0; i < MAX_KEYBOARDS; i++)
		if (keyboards[i].type == type) {
			*keyboard = keyboards[i].dp;
			return 1;
		}

	printf("no such device found\n");
	return 0;
}

int
Xkeyboard()
{
	struct device_path keyboard;
	int rc;

	/* walk the device list if not already done */
	if (walked == 0) {
		bus_walk(NULL, MAXMODBUS);
		walked++;
	}

	if (sstorsiz < KEYBOARDSIZE) {
		printf("no keyboard information in stable storage\n");
		return 0;
	}

	if (cmd.argc == 1) {
		print_keyboard();
	} else {
		keyboard = sstor.ss_keyboard;
		if (set_keyboard(&keyboard)) {
			if (memcmp(&sstor.ss_keyboard, &keyboard,
			    sizeof keyboard) != 0) {
				sstor.ss_keyboard = keyboard;

				/* alea jacta est */
				rc = (*pdc)(PDC_STABLE, PDC_STABLE_WRITE,
				    KEYBOARDOFFSET, &sstor.ss_keyboard,
				    sizeof(sstor.ss_keyboard));
				if (rc != 0) {
					printf("failed to save keyboard"
					    " settings, error %d\n", rc);
					/* read sstor again for safety */
					(*pdc)(PDC_STABLE, PDC_STABLE_READ,
					    KEYBOARDOFFSET, &sstor.ss_keyboard,
					    sizeof(sstor.ss_keyboard));
				} else
					printf("you will need to power-cycle "
					       "your machine for the changes "
					       "to take effect.\n");
			}
			print_keyboard();
		}
	}

	return 0;
}

/*
 * U T I L I T I E S
 */

/*
 * Bus walker.
 * This routine will walk all the modules on a given bus, registering
 * serial ports, keyboard and graphics devices as they are found.
 */
void
bus_walk(struct device_path *idp, int maxmod)
{
	struct device_path dp;
	struct pdc_memmap memmap;
	struct iodc_data mptr;
	int err, i, kluge_ps2 = 0;	/* kluge, see below */

	for (i = 0; i < maxmod; i++) {
		if (idp) {
			dp = *idp;
			path_shift(&dp, i);
		} else {
			dp.dp_flags = 0;
			dp.dp_bc[0] = dp.dp_bc[1] = dp.dp_bc[2] =
			dp.dp_bc[3] = dp.dp_bc[4] = dp.dp_bc[5] = -1;
			dp.dp_mod = i;
			bzero(&dp.dp_layers, sizeof dp.dp_layers);
		}

		if ((pdc)(PDC_MEMMAP, PDC_MEMMAP_HPA, &memmap, &dp) < 0 &&
		    (pdc)(PDC_SYSMAP, PDC_SYSMAP_HPA, &memmap, &dp) < 0)
			continue;

		if ((err = (pdc)(PDC_IODC, PDC_IODC_READ, &pdcbuf, memmap.hpa,
		    IODC_DATA, &mptr, sizeof(mptr))) < 0)
			continue;

#ifdef DEBUG
		printf("device %d/%d/%d/%d/%d/%d "
		    "flags %d mod %x type %x model %x\n",
		    dp.dp_bc[0], dp.dp_bc[1], dp.dp_bc[2], dp.dp_bc[3],
		    dp.dp_bc[4], dp.dp_bc[5], dp.dp_flags, dp.dp_mod,
		    mptr.iodc_type, mptr.iodc_sv_model);
#endif

		/*
		 * If the device can be considered as a valid rs232,
		 * graphics console or keyboard, register it.
		 *
		 * Unfortunately, devices which should be considered as
		 * ``main'' aren't necessarily seen first.
		 * The rules we try to enforce here are as follows:
		 * - GIO PS/2 ports wins over any other PS/2 port.
		 * - the first GIO serial found wins over any other
		 *   serial port.
		 * The second rule is a bit tricky to achieve, since on
		 * some machines (for example, 715/100XC), the two serial
		 * ports are not seen as attached to the same busses...
		 */
		switch (mptr.iodc_type) {
		case HPPA_TYPE_BCPORT:
			bus_walk(&dp, MAXMODBUS);
			break;
		case HPPA_TYPE_BRIDGE:
			if (mptr.iodc_sv_model == HPPA_BRIDGE_DINO) {
				pci_bus_walk(&dp, &mptr, &memmap);
				break;
			}
			/* FALLTHROUGH */
		case HPPA_TYPE_BHA:
			/* if there was no phantomas(4) here */
			if (dp.dp_bc[5] == -1)
				path_shift(&dp, 0);
			bus_walk(&dp, MAXMODBUS);
			break;
		case HPPA_TYPE_ADIRECT:
			switch (mptr.iodc_sv_model) {
			case HPPA_ADIRECT_RS232:
				register_device(serials, MAX_SERIALS,
				    &dp, &mptr, SERIAL, 0);
				break;
			case HPPA_ADIRECT_HIL:
				register_device(keyboards, MAX_KEYBOARDS,
				    &dp, &mptr, HIL, 0);
				break;
			case HPPA_ADIRECT_PEACOCK:
			case HPPA_ADIRECT_LEONARDO:
				register_device(graphics, MAX_GRAPHICS,
				    &dp, &mptr, GRAPHICS, 0);
				break;
			}
			break;
		case HPPA_TYPE_FIO:
			switch (mptr.iodc_sv_model) {
			case HPPA_FIO_HIL:
				register_device(keyboards, MAX_KEYBOARDS,
				    &dp, &mptr, HIL, 0);
				break;
			case HPPA_FIO_RS232:	/* com@gsc */
				register_device(serials, MAX_SERIALS,
				    &dp, &mptr, SERIAL, 0);
				break;
			case HPPA_FIO_DINOPCK:
				register_device(keyboards, MAX_KEYBOARDS,
				    &dp, &mptr, PS2, 0);
				break;
			case HPPA_FIO_GPCIO:
				/*
				 * KLUGE! At this point, there is no way to
				 * know if this port is the keyboard port or
				 * the mouse port.
				 * Let's assume the first port found is the
				 * keyboard, and ignore the others.
				 */
				if (kluge_ps2 != 0)
					break;
				register_device(keyboards, MAX_KEYBOARDS,
				    &dp, &mptr, PS2, 1);
				kluge_ps2++;
				break;
			case HPPA_FIO_GRS232:	/* com@dino, com@gsc */
			{
				int j, first;

				/*
				 * If a GIO serial port is already registered,
				 * register as extra port...
				 */
				first = 1;
				for (j = 0; j < MAX_SERIALS; j++)
					if (serials[j].type == SERIAL &&
					    serials[j].iodc_type ==
					      HPPA_TYPE_FIO &&
					    serials[j].iodc_model ==
					      HPPA_FIO_GRS232) {
						first = 0;
						break;
					}

				register_device(serials, MAX_SERIALS,
				    &dp, &mptr, SERIAL, first);
			}
				break;
			case HPPA_FIO_SGC:
				register_device(graphics, MAX_GRAPHICS,
				    &dp, &mptr, GRAPHICS, 0);
				break;
			case HPPA_FIO_GSGC:
				register_device(graphics, MAX_GRAPHICS,
				    &dp, &mptr, GRAPHICS, 1);
				break;
#if 0 /* can these really be used as console? */
			case HPPA_FIO_GRJ16:	/* com@gsc */
				register_device(serials, MAX_SERIALS,
				    &dp, &mptr, SERIAL, 0);
				break;
#endif
			}
			break;
		case HPPA_TYPE_IOA:
			switch (mptr.iodc_sv_model) {
			case HPPA_IOA_UTURN:
				bus_walk(&dp, MAXMODBUS - 1);
				break;
			}
			break;
		}
	}
}

/*
 * PCI bus walker.
 * The PDC device enumeration stops at the PCI bridge level, however
 * in order to properly handle console path on systems with PCI graphics
 * and USB controllers, it is necessary to dig further.
 *
 * Note that there are apparently PDC routines to access bridge configuration
 * space, but I have yet to find documentation about them.
 *
 * We ignore multi-function devices and subordinate PCI busses here, since
 * PDC PCI device paths stop at the PCI device number, and subordinate
 * busses are unlikely to be configured by the PDC.
 */

#define	ELROY_MODEL	0x78
#define	DINO_PAMR	0x804
#define	DINO_CFG_ADDR	0x64
#define	DINO_CFG_DATA	0x68

void
pci_bus_walk(struct device_path *idp, struct iodc_data *mptr,
    struct pdc_memmap *memmap)
{
	struct device_path dp;
	int dev, fn, nfuncs;
	uint32_t id, bhlcr, class;
	uint32_t (*conf_read)(u_int, int, int, u_int);

	if (mptr->iodc_model == ELROY_MODEL)
		conf_read = elroy_conf_read;
	else
		conf_read = dino_conf_read;

	for (dev = 0; dev < 32; dev++) {
		id = (*conf_read)(memmap->hpa, dev, 0, PCI_ID_REG);

		if (PCI_VENDOR(id) == PCI_VENDOR_INVALID || PCI_VENDOR(id) == 0)
			continue;

		bhlcr = (*conf_read)(memmap->hpa, dev, 0, PCI_BHLC_REG);
		nfuncs = PCI_HDRTYPE_MULTIFN(bhlcr) ? 8 : 1;

		for (fn = 0; fn < nfuncs; fn++) {
			dp = *idp;
			path_shift(&dp, dev);
			path_shift(&dp, fn);
			
			if (fn != 0)
				id = (*conf_read)(memmap->hpa, dev, fn,
				    PCI_ID_REG);
			class = (*conf_read)(memmap->hpa, dev, fn,
			    PCI_CLASS_REG);

			/*
			 * We are only interested in two kinds of devices
			 * here: sti graphics, and USB controllers.
			 */
			if (PCI_CLASS(class) == PCI_CLASS_SERIALBUS &&
			    PCI_SUBCLASS(class) == PCI_SUBCLASS_SERIALBUS_USB) {
				/*
				 * Note about the last parameter of the
				 * register_device() call below being zero:
				 * machines with USB keyboards have neither
				 * PS/2 nor HIL controllers, so it doesn't
				 * matter what order the USB controllers are
				 * in.
				 * However machines with PS/2 keyboards
				 * might have an USB PCI card plugged in,
				 * which better appear after the PS/2
				 * keyboard.
				 */
				register_device(keyboards, MAX_KEYBOARDS,
				    &dp, mptr, USB, 0);
				continue;
			}

			switch (PCI_VENDOR(id)) {
			case PCI_VENDOR_HP:
				switch (PCI_PRODUCT(id)) {
				case PCI_PRODUCT_HP_VISUALIZE_EG:
				case PCI_PRODUCT_HP_VISUALIZE_FX2:
				case PCI_PRODUCT_HP_VISUALIZE_FX4:
				case PCI_PRODUCT_HP_VISUALIZE_FX6:
				case PCI_PRODUCT_HP_VISUALIZE_FXE:
					register_device(graphics, MAX_GRAPHICS,
					    &dp, mptr, GRAPHICS, 0);
					break;
				}
				break;
			case PCI_VENDOR_NS:
				if (PCI_PRODUCT(id) == PCI_PRODUCT_NS_PC87560) {
					/* serial_2 */
					path_shift(&dp, 2);
					register_device(serials, MAX_SERIALS,
					    &dp, mptr, SERIAL, 1);
					/* serial_1 */
					dp.dp_mod = 1;
					register_device(serials, MAX_SERIALS,
					    &dp, mptr, SERIAL, 1);
				}
				break;
			}
		}
	}
}

uint32_t
dino_conf_read(u_int hpa, int dev, int fn, u_int reg)
{
	volatile uint32_t *dino = (volatile uint32_t *)hpa;
	uint32_t pamr;
	uint32_t addr, id;

	addr = (dev << 11) | (fn << 8) | reg;

	pamr = dino[DINO_PAMR / 4];
	dino[DINO_PAMR / 4] = 0;
	dino[DINO_CFG_ADDR / 4] = addr;
	id = dino[DINO_CFG_DATA / 4];
	dino[DINO_PAMR / 4] = pamr;

	return letoh32(id);
}

uint32_t
elroy_conf_read(u_int hpa, int dev, int fn, u_int reg)
{
	volatile struct elroy_regs *elroy = (volatile struct elroy_regs *)hpa;
	uint32_t arb_mask, err_cfg, control;
	uint32_t addr, id;

	addr = (dev << 11) | (fn << 8) | reg;

	arb_mask = *(volatile uint32_t *)&elroy->arb_mask;
	err_cfg = *(volatile uint32_t *)&elroy->err_cfg;
	control = *(volatile uint32_t *)&elroy->control;

	if (arb_mask == 0)
		*(volatile uint32_t *)&elroy->arb_mask =
		    htole32(ELROY_ARB_ENABLE);
	*(volatile uint32_t *)&elroy->err_cfg = err_cfg |
	    htole32(ELROY_ERRCFG_SMART | ELROY_ERRCFG_CM);
	*(volatile uint32_t *)&elroy->control =
	    (control | htole32(ELROY_CONTROL_CE)) & ~htole32(ELROY_CONTROL_HF);

	*(volatile uint32_t *)&elroy->pci_conf_addr = htole32(addr);
	addr = *(volatile uint32_t *)&elroy->pci_conf_addr;
	id = *(volatile uint32_t *)&elroy->pci_conf_data;

	*(volatile uint32_t *)&elroy->control =
	    control | htole32(ELROY_CONTROL_CE | ELROY_CONTROL_CL);
	*(volatile uint32_t *)&elroy->control = control;
	*(volatile uint32_t *)&elroy->err_cfg = err_cfg;
	if (arb_mask == 0)
		*(volatile uint32_t *)&elroy->arb_mask = arb_mask;

	return letoh32(id);
}

/*
 * Relaxed device comparison
 */
int
path_match(struct device_path *dev1, struct device_path *dev2)
{
	return dev1->dp_mod == dev2->dp_mod &&
	    dev1->dp_bc[0] == dev2->dp_bc[0] &&
	    dev1->dp_bc[1] == dev2->dp_bc[1] &&
	    dev1->dp_bc[2] == dev2->dp_bc[2] &&
	    dev1->dp_bc[3] == dev2->dp_bc[3] &&
	    dev1->dp_bc[4] == dev2->dp_bc[4] &&
	    dev1->dp_bc[5] == dev2->dp_bc[5];
}

/*
 * Shift a device path, inserting a new value as dp_mod.
 */
void
path_shift(struct device_path *dp, int nmod)
{
	dp->dp_bc[0] = dp->dp_bc[1];
	dp->dp_bc[1] = dp->dp_bc[2];
	dp->dp_bc[2] = dp->dp_bc[3];
	dp->dp_bc[3] = dp->dp_bc[4];
	dp->dp_bc[4] = dp->dp_bc[5];
	dp->dp_bc[5] = dp->dp_mod;
	dp->dp_mod = nmod;
}

void
register_device(devlist, cnt, dp, mptr, type, first)
	struct consoledev *devlist;
	int cnt;
	struct device_path *dp;
	struct iodc_data *mptr;
	int type;
	int first;
{
	int i;
	struct consoledev *dev;

	for (i = 0, dev = devlist; i < cnt; i++, dev++)
		if (dev->type == 0)
			break;

	if (i == cnt) {
#ifdef DEBUG
		printf("can't register device, need more room!\n");
#endif
		return;
	}

	/*
	 * If this is supposedly the main device, insert on top
	 */
	if (first != 0) {
		memcpy(devlist + 1, devlist,
		    (cnt - 1) * sizeof(struct consoledev));
		dev = devlist;
	}

	dev->dp = *dp;
	dev->type = type;
	dev->iodc_type = mptr->iodc_type;
	dev->iodc_model = mptr->iodc_sv_model;

#ifdef DEBUG
	printf("(registered as type %d)\n", type);
#endif
}
