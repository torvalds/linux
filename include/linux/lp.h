/*
 * usr/include/linux/lp.h c.1991-1992 James Wiegand
 * many modifications copyright (C) 1992 Michael K. Johnson
 * Interrupt support added 1993 Nigel Gamble
 * Removed 8255 status defines from inside __KERNEL__ Marcelo Tosatti 
 */
#ifndef _LINUX_LP_H
#define _LINUX_LP_H


#include <linux/wait.h>
#include <linux/mutex.h>
#include <uapi/linux/lp.h>

/* Magic numbers for defining port-device mappings */
#define LP_PARPORT_UNSPEC -4
#define LP_PARPORT_AUTO -3
#define LP_PARPORT_OFF -2
#define LP_PARPORT_NONE -1

#define LP_F(minor)	lp_table[(minor)].flags		/* flags for busy, etc. */
#define LP_CHAR(minor)	lp_table[(minor)].chars		/* busy timeout */
#define LP_TIME(minor)	lp_table[(minor)].time		/* wait time */
#define LP_WAIT(minor)	lp_table[(minor)].wait		/* strobe wait */
#define LP_IRQ(minor)	lp_table[(minor)].dev->port->irq /* interrupt # */
					/* PARPORT_IRQ_NONE means polled */
#ifdef LP_STATS
#define LP_STAT(minor)	lp_table[(minor)].stats		/* statistics area */
#endif
#define LP_BUFFER_SIZE PAGE_SIZE

#define LP_BASE(x)	lp_table[(x)].dev->port->base

#ifdef LP_STATS
struct lp_stats {
	unsigned long chars;
	unsigned long sleeps;
	unsigned int maxrun;
	unsigned int maxwait;
	unsigned int meanwait;
	unsigned int mdev;
};
#endif

struct lp_struct {
	struct pardevice *dev;
	unsigned long flags;
	unsigned int chars;
	unsigned int time;
	unsigned int wait;
	char *lp_buffer;
#ifdef LP_STATS
	unsigned int lastcall;
	unsigned int runchars;
	struct lp_stats stats;
#endif
	wait_queue_head_t waitq;
	unsigned int last_error;
	struct mutex port_mutex;
	wait_queue_head_t dataq;
	long timeout;
	unsigned int best_mode;
	unsigned int current_mode;
	unsigned long bits;
};

/*
 * The following constants describe the various signals of the printer port
 * hardware.  Note that the hardware inverts some signals and that some
 * signals are active low.  An example is LP_STROBE, which must be programmed
 * with 1 for being active and 0 for being inactive, because the strobe signal
 * gets inverted, but it is also active low.
 */


/* 
 * defines for 8255 control port
 * base + 2 
 * accessed with LP_C(minor)
 */
#define LP_PINTEN	0x10  /* high to read data in or-ed with data out */
#define LP_PSELECP	0x08  /* inverted output, active low */
#define LP_PINITP	0x04  /* unchanged output, active low */
#define LP_PAUTOLF	0x02  /* inverted output, active low */
#define LP_PSTROBE	0x01  /* short high output on raising edge */

/* 
 * the value written to ports to test existence. PC-style ports will 
 * return the value written. AT-style ports will return 0. so why not
 * make them the same ? 
 */
#define LP_DUMMY	0x00

/*
 * This is the port delay time, in microseconds.
 * It is used only in the lp_init() and lp_reset() routine.
 */
#define LP_DELAY 	50

#endif
