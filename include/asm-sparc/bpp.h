#ifndef _SPARC_BPP_H
#define _SPARC_BPP_H

/*
 * Copyright (c) 1995 Picture Elements
 *	Stephen Williams
 *	Gus Baldauf
 *
 * Linux/SPARC port by Peter Zaitcev.
 * Integration into SPARC tree by Tom Dyas.
 */

#include  <linux/ioctl.h>

/*
 * This is a driver that supports IEEE Std 1284-1994 communications
 * with compliant or compatible devices. It will use whatever features
 * the device supports, prefering those that are typically faster.
 *
 * When the device is opened, it is left in COMPATIBILITY mode, and
 * writes work like any printer device. The driver only attempt to
 * negotiate 1284 modes when needed so that plugs can be pulled,
 * switch boxes switched, etc., without disrupting things. It will
 * also leave the device in compatibility mode when closed.
 */



/*
 * This driver also supplies ioctls to manually manipulate the
 * pins. This is great for testing devices, or writing code to deal
 * with bizzarro-mode of the ACME Special TurboThingy Plus.
 *
 * NOTE: These ioctl currently do not interact well with
 * read/write. Caveat emptor.
 *
 * PUT_PINS allows us to assign the sense of all the pins, including
 * the data pins if being driven by the host. The GET_PINS returns the
 * pins that the peripheral drives, including data if appropriate.
 */

# define BPP_PUT_PINS _IOW('B', 1, int)
# define BPP_GET_PINS _IOR('B', 2, char) /* that's bogus - should've been _IO */
# define BPP_PUT_DATA _IOW('B', 3, int)
# define BPP_GET_DATA _IOR('B', 4, char) /* ditto */

/*
 * Set the data bus to input mode. Disengage the data bin driver and
 * be prepared to read values from the peripheral. If the arg is 0,
 * then revert the bus to output mode.
 */
# define BPP_SET_INPUT _IOW('B', 5, int)

/*
 * These bits apply to the PUT operation...
 */
# define BPP_PP_nStrobe   0x0001
# define BPP_PP_nAutoFd   0x0002
# define BPP_PP_nInit     0x0004
# define BPP_PP_nSelectIn 0x0008

/*
 * These apply to the GET operation, which also reads the current value
 * of the previously put values. A bit mask of these will be returned
 * as a bit mask in the return code of the ioctl().
 */
# define BPP_GP_nAck   0x0100
# define BPP_GP_Busy   0x0200
# define BPP_GP_PError 0x0400
# define BPP_GP_Select 0x0800
# define BPP_GP_nFault 0x1000

#endif
