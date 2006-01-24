/*
 * Hardware specific definitions for SL-C1000 (Akita)
 *
 * Copyright (c) 2005 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* Akita IO Expander GPIOs */

#define AKITA_IOEXP_RESERVED_7      (1 << 7)
#define AKITA_IOEXP_IR_ON           (1 << 6)
#define AKITA_IOEXP_AKIN_PULLUP     (1 << 5)
#define AKITA_IOEXP_BACKLIGHT_CONT  (1 << 4)
#define AKITA_IOEXP_BACKLIGHT_ON    (1 << 3)
#define AKITA_IOEXP_MIC_BIAS        (1 << 2)
#define AKITA_IOEXP_RESERVED_1      (1 << 1)
#define AKITA_IOEXP_RESERVED_0      (1 << 0)

/* Direction Bitfield  0=output  1=input */
#define AKITA_IOEXP_IO_DIR	0
/* Default Values */
#define AKITA_IOEXP_IO_OUT	(AKITA_IOEXP_IR_ON | AKITA_IOEXP_AKIN_PULLUP)

extern struct platform_device akitaioexp_device;

void akita_set_ioexp(struct device *dev, unsigned char bitmask);
void akita_reset_ioexp(struct device *dev, unsigned char bitmask);

