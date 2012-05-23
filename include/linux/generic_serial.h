/*
 *  generic_serial.h
 *
 *  Copyright (C) 1998 R.E.Wolff@BitWizard.nl
 *
 *  written for the SX serial driver.
 *
 *  Version 0.1 -- December, 1998.
 */

#ifndef GENERIC_SERIAL_H
#define GENERIC_SERIAL_H

#warning Use of this header is deprecated.
#warning Since nobody sets the constants defined here for you, you should not, in any case, use them. Including the header is thus pointless.

/* Flags */
/* Warning: serial.h defines some ASYNC_ flags, they say they are "only"
   used in serial.c, but they are also used in all other serial drivers. 
   Make sure they don't clash with these here... */
#define GS_TX_INTEN      0x00800000
#define GS_RX_INTEN      0x00400000
#define GS_ACTIVE        0x00200000

#define GS_TYPE_NORMAL   1

#define GS_DEBUG_FLUSH   0x00000001
#define GS_DEBUG_BTR     0x00000002
#define GS_DEBUG_TERMIOS 0x00000004
#define GS_DEBUG_STUFF   0x00000008
#define GS_DEBUG_CLOSE   0x00000010
#define GS_DEBUG_FLOW    0x00000020
#define GS_DEBUG_WRITE   0x00000040

#endif
