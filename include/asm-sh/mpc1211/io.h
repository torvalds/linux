/*
 * include/asm-sh/mpc1211/io.h
 *
 * Copyright 2001 Saito.K & Jeanne
 *
 * IO functions for an Interface MPC-1211
 */

#ifndef _ASM_SH_IO_MPC1211_H
#define _ASM_SH_IO_MPC1211_H

#include <linux/time.h>

extern int mpc1211_irq_demux(int irq);

extern void init_mpc1211_IRQ(void);
extern void heartbeat_mpc1211(void);

extern void mpc1211_rtc_gettimeofday(struct timeval *tv);
extern int mpc1211_rtc_settimeofday(const struct timeval *tv);

#endif /* _ASM_SH_IO_MPC1211_H */
