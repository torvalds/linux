/*
 * RTC I/O Bridge interfaces for CSR SiRFprimaII
 * ARM access the registers of SYSRTC, GPSRTC and PWRC through this module
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#ifndef _SIRFSOC_RTC_IOBRG_H_
#define _SIRFSOC_RTC_IOBRG_H_

struct regmap_config;

extern void sirfsoc_rtc_iobrg_besyncing(void);

extern u32 sirfsoc_rtc_iobrg_readl(u32 addr);

extern void sirfsoc_rtc_iobrg_writel(u32 val, u32 addr);
struct regmap *devm_regmap_init_iobg(struct device *dev,
				    const struct regmap_config *config);

#endif
