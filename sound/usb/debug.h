/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_DE_H
#define __USBAUDIO_DE_H

/*
 * h/w constraints
 */

#ifdef HW_CONST_DE
#define hwc_de(fmt, args...) printk(KERN_DE fmt, ##args)
#else
#define hwc_de(fmt, args...) do { } while(0)
#endif

#endif /* __USBAUDIO_DE_H */

