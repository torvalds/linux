/*
 * Control LCD module hung off parallel port using the
 * ppi 'geek port' interface.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sysexits.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppi.h>

#define debug(lev, fmt, args...)	if (debuglevel >= lev) fprintf(stderr, fmt "\n" , ## args);

static void	usage(void);
static char	*progname;

#define	DEFAULT_DEVICE	"/dev/ppi0"

/* Driver functions */
static void	hd44780_prepare(char *devname, char *options);
static void	hd44780_finish(void);
static void	hd44780_command(int cmd);
static void	hd44780_putc(int c);

/* 
 * Commands 
 * Note that unrecognised command escapes are passed through with
 * the command value set to the ASCII value of the escaped character.
 */
#define CMD_RESET	0
#define CMD_BKSP	1
#define CMD_CLR		2
#define CMD_NL		3
#define CMD_CR		4
#define CMD_HOME	5

#define MAX_DRVOPT	10	/* maximum driver-specific options */

struct lcd_driver
{
    char	*l_code;
    char	*l_name;
    char	*l_options[MAX_DRVOPT];
    void	(* l_prepare)(char *name, char *options);
    void	(* l_finish)(void);
    void	(* l_command)(int cmd);
    void	(* l_putc)(int c);
};

static struct lcd_driver lcd_drivertab[] = {
    {
	"hd44780", 
	"Hitachi HD44780 and compatibles", 
	{
	    "Reset options:",
	    "    1     1-line display (default 2)",
	    "    B     Cursor blink enable",
	    "    C     Cursor enable",
	    "    F     Large font select",
	    NULL
	},
	hd44780_prepare, 
	hd44780_finish,
	hd44780_command, 
	hd44780_putc
    },
    {
	NULL, 
	NULL, 
	{
	    NULL
	}, 
	NULL, 
	NULL
    }
};

static void	do_char(struct lcd_driver *driver, char ch);

int	debuglevel = 0;
int	vflag = 0;

int
main(int argc, char *argv[]) 
{
    extern char		*optarg;
    extern int		optind;
    struct lcd_driver	*driver = &lcd_drivertab[0];
    char		*drivertype, *cp;
    char		*devname = DEFAULT_DEVICE;
    char		*drvopts = NULL;
    int			ch, i;

    if ((progname = strrchr(argv[0], '/'))) {
	progname++;
    } else {
	progname = argv[0];
    }

    drivertype = getenv("LCD_TYPE");
    
    while ((ch = getopt(argc, argv, "Dd:f:o:v")) != -1) {
	switch(ch) {
	case 'D':
	    debuglevel++;
	    break;
	case 'd':
	    drivertype = optarg;
	    break;
	case 'f':
	    devname = optarg;
	    break;
	case 'o':
	    drvopts = optarg;
	    break;
	case 'v':
	    vflag = 1;
	    break;
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;
    
    /* If an LCD type was specified, look it up */
    if (drivertype != NULL) {
	driver = NULL;
	for (i = 0; lcd_drivertab[i].l_code != NULL; i++) {
	    if (!strcmp(drivertype, lcd_drivertab[i].l_code)) {
		driver = &lcd_drivertab[i];
		break;
	    }
	}
	if (driver == NULL) {
	    warnx("LCD driver '%s' not known", drivertype);
	    usage();
	}
    }
    debug(1, "Driver selected for %s", driver->l_name);
    driver->l_prepare(devname, drvopts);
    atexit(driver->l_finish);

    if (argc > 0) {
	debug(2, "reading input from %d argument%s", argc, (argc > 1) ? "s" : "");
	for (i = 0; i < argc; i++)
	    for (cp = argv[i]; *cp; cp++)
		do_char(driver, *cp);
    } else {
	debug(2, "reading input from stdin");
	setvbuf(stdin, NULL, _IONBF, 0);
	while ((ch = fgetc(stdin)) != EOF)
	    do_char(driver, (char)ch);
    }
    exit(EX_OK);
}

static void
usage(void) 
{
    int		i, j;
    
    fprintf(stderr, "usage: %s [-v] [-d drivername] [-f device] [-o options] [args...]\n", progname);
    fprintf(stderr, "   -D      Increase debugging\n");
    fprintf(stderr, "   -f      Specify device, default is '%s'\n", DEFAULT_DEVICE);
    fprintf(stderr, "   -d      Specify driver, one of:\n");
    for (i = 0; lcd_drivertab[i].l_code != NULL; i++) {
	fprintf(stderr, "              %-10s (%s)%s\n", 
		lcd_drivertab[i].l_code, lcd_drivertab[i].l_name, (i == 0) ? " *default*" : "");
	if (lcd_drivertab[i].l_options[0] != NULL) {
	    
	    for (j = 0; lcd_drivertab[i].l_options[j] != NULL; j++)
		fprintf(stderr, "                  %s\n", lcd_drivertab[i].l_options[j]);
	}
    }
    fprintf(stderr, "  -o       Specify driver option string\n");
    fprintf(stderr, "  args     Message strings.  Embedded escapes supported:\n");
    fprintf(stderr, "                  \\b	Backspace\n");
    fprintf(stderr, "                  \\f	Clear display, home cursor\n");
    fprintf(stderr, "                  \\n	Newline\n");
    fprintf(stderr, "                  \\r	Carriage return\n");
    fprintf(stderr, "                  \\R	Reset display\n");
    fprintf(stderr, "                  \\v	Home cursor\n");
    fprintf(stderr, "                  \\\\	Literal \\\n");
    fprintf(stderr, "           If args not supplied, strings are read from standard input\n");
    exit(EX_USAGE);
}

static void
do_char(struct lcd_driver *driver, char ch)
{
    static int	esc = 0;
    
    if (esc) {
	switch(ch) {
	case 'b':
	    driver->l_command(CMD_BKSP);
	    break;
	case 'f':
	    driver->l_command(CMD_CLR);
	    break;
	case 'n':
	    driver->l_command(CMD_NL);
	    break;
	case 'r':
	    driver->l_command(CMD_CR);
	    break;
	case 'R':
	    driver->l_command(CMD_RESET);
	    break;
	case 'v':
	    driver->l_command(CMD_HOME);
	    break;
	case '\\':
	    driver->l_putc('\\');
	    break;
	default:
	    driver->l_command(ch);
	    break;
	}
	esc = 0;
    } else {
	if (ch == '\\') {
	    esc = 1;
	} else {
	    if (vflag || isprint(ch))
		driver->l_putc(ch);
	}
    }
}


/******************************************************************************
 * Driver for the Hitachi HD44780.  This is probably *the* most common driver
 * to be found on one- and two-line alphanumeric LCDs.
 *
 * This driver assumes the following connections :
 *
 * Parallel Port	LCD Module
 * --------------------------------
 * Strobe (1)		Enable (6)
 * Data (2-9)		Data (7-14)
 * Select In (17)	RS (4)
 * Auto Feed (14)	R/W (5)
 *
 * In addition, power must be supplied to the module, normally with
 * a circuit similar to this:
 *
 * VCC (+5V) O------o-------o--------O Module pin 2
 *                  |       | +
 *                  /      ---
 *                  \      --- 1uF
 *                  /       | -
 *                  \ <-----o--------O Module pin 3
 *                  /
 *                  \
 *                  |
 * GND       O------o----------------O Module pin 1
 *
 * The ground line should also be connected to the parallel port, on
 * one of the ground pins (eg. pin 25).
 *
 * Note that the pinning on some LCD modules has the odd and even pins
 * arranged as though reversed; check carefully before connecting a module
 * as it is possible to toast the HD44780 if the power is reversed.
 */

static int	hd_fd;
static u_int8_t	hd_cbits;
static int	hd_lines = 2;
static int	hd_blink = 0;
static int 	hd_cursor = 0;
static int	hd_font = 0;

#define HD_COMMAND	SELECTIN
#define HD_DATA		0
#define HD_READ		0
#define HD_WRITE	AUTOFEED

#define HD_BF		0x80		/* internal busy flag */
#define HD_ADDRMASK	0x7f		/* DDRAM address mask */

#define hd_sctrl(v)	{u_int8_t _val; _val = hd_cbits | v; ioctl(hd_fd, PPISCTRL, &_val);}
#define hd_sdata(v)	{u_int8_t _val; _val = v; ioctl(hd_fd, PPISDATA, &_val);}
#define hd_gdata(v)	ioctl(hd_fd, PPIGDATA, &v)

static void
hd44780_output(int type, int data)
{
    debug(3, "%s -> 0x%02x", (type == HD_COMMAND) ? "cmd " : "data", data);
    hd_sctrl(type | HD_WRITE | STROBE);	/* set direction, address */
    hd_sctrl(type | HD_WRITE);		/* raise E */
    hd_sdata((u_int8_t) data);		/* drive data */
    hd_sctrl(type | HD_WRITE | STROBE);	/* lower E */
}

static int
hd44780_input(int type) 
{
    u_int8_t	val;

    hd_sctrl(type | HD_READ | STROBE);	/* set direction, address */ 
    hd_sctrl(type | HD_READ);		/* raise E */
    hd_gdata(val);			/* read data */
    hd_sctrl(type | HD_READ | STROBE);	/* lower E */

    debug(3, "0x%02x -> %s", val, (type == HD_COMMAND) ? "cmd " : "data");
    return(val);
}

static void
hd44780_prepare(char *devname, char *options) 
{
    char	*cp = options;
    
    if ((hd_fd = open(devname, O_RDWR, 0)) == -1)
	err(EX_OSFILE, "can't open '%s'", devname);

    /* parse options */
    while (cp && *cp) {
	switch (*cp++) {
	case '1':
	    hd_lines = 1;
	    break;
	case 'B':
	    hd_blink = 1;
	    break;
	case 'C':
	    hd_cursor = 1;
	    break;
	case 'F':
	    hd_font = 1;
	    break;
	default:
	    errx(EX_USAGE, "hd44780: unknown option code '%c'", *(cp-1));
	}
    }

    /* Put LCD in idle state */
    if (ioctl(hd_fd, PPIGCTRL, &hd_cbits))		/* save other control bits */
	err(EX_IOERR, "ioctl PPIGCTRL failed (not a ppi device?)");
    hd_cbits &= ~(STROBE | SELECTIN | AUTOFEED);	/* set strobe, RS, R/W low */
    debug(2, "static control bits 0x%x", hd_cbits);
    hd_sctrl(STROBE);
    hd_sdata(0);

}

static void
hd44780_finish(void) 
{
    close(hd_fd);
}

static void
hd44780_command(int cmd) 
{
    u_int8_t	val;

    switch (cmd) {
    case CMD_RESET:	/* full manual reset and reconfigure as per datasheet */
	debug(1, "hd44780: reset to %d lines, %s font,%s%s cursor", 
	      hd_lines, hd_font ? "5x10" : "5x7", hd_cursor ? "" : " no", hd_blink ? " blinking" : "");
	val = 0x30;
	if (hd_lines == 2)
	    val |= 0x08;
	if (hd_font)
	    val |= 0x04;
	hd44780_output(HD_COMMAND, val);
	usleep(10000);
	hd44780_output(HD_COMMAND, val);
	usleep(1000);
	hd44780_output(HD_COMMAND, val);
	usleep(1000);
	val = 0x08;				/* display off */
	hd44780_output(HD_COMMAND, val);
	usleep(1000);
	val |= 0x04;				/* display on */
	if (hd_cursor)
	    val |= 0x02;
	if (hd_blink)
	    val |= 0x01;
	hd44780_output(HD_COMMAND, val);
	usleep(1000);
	hd44780_output(HD_COMMAND, 0x06);	/* shift cursor by increment */
	usleep(1000);
	/* FALLTHROUGH */

    case CMD_CLR:
	hd44780_output(HD_COMMAND, 0x01);
	usleep(2000);
	break;

    case CMD_BKSP:
	hd44780_output(HD_DATA, 0x10);		/* shift cursor left one */
	break;
	
    case CMD_NL:
	if (hd_lines == 2)
	    hd44780_output(HD_COMMAND, 0xc0);	/* beginning of second line */
	break;
	
    case CMD_CR:
	/* XXX will not work in 4-line mode, or where readback fails */
	val = hd44780_input(HD_COMMAND) & 0x3f;	/* mask character position, save line pos */
	hd44780_output(HD_COMMAND, 0x80 | val);
	break;
	
    case CMD_HOME:
	hd44780_output(HD_COMMAND, 0x02);
	usleep(2000);
	break;
	
    default:
	if (isprint(cmd)) {
	    warnx("unknown command %c", cmd);
	} else {
	    warnx("unknown command 0x%x", cmd);
	}
    }
    usleep(40);
}

static void
hd44780_putc(int c)
{
    hd44780_output(HD_DATA, c);
    usleep(40);
}

