/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_SYSFB_H
#define _LINUX_SYSFB_H

/*
 * Generic System Framebuffers on x86
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/platform_data/simplefb.h>

struct screen_info;

enum {
	M_I17,		/* 17-Inch iMac */
	M_I20,		/* 20-Inch iMac */
	M_I20_SR,	/* 20-Inch iMac (Santa Rosa) */
	M_I24,		/* 24-Inch iMac */
	M_I24_8_1,	/* 24-Inch iMac, 8,1th gen */
	M_I24_10_1,	/* 24-Inch iMac, 10,1th gen */
	M_I27_11_1,	/* 27-Inch iMac, 11,1th gen */
	M_MINI,		/* Mac Mini */
	M_MINI_3_1,	/* Mac Mini, 3,1th gen */
	M_MINI_4_1,	/* Mac Mini, 4,1th gen */
	M_MB,		/* MacBook */
	M_MB_2,		/* MacBook, 2nd rev. */
	M_MB_3,		/* MacBook, 3rd rev. */
	M_MB_5_1,	/* MacBook, 5th rev. */
	M_MB_6_1,	/* MacBook, 6th rev. */
	M_MB_7_1,	/* MacBook, 7th rev. */
	M_MB_SR,	/* MacBook, 2nd gen, (Santa Rosa) */
	M_MBA,		/* MacBook Air */
	M_MBA_3,	/* Macbook Air, 3rd rev */
	M_MBP,		/* MacBook Pro */
	M_MBP_2,	/* MacBook Pro 2nd gen */
	M_MBP_2_2,	/* MacBook Pro 2,2nd gen */
	M_MBP_SR,	/* MacBook Pro (Santa Rosa) */
	M_MBP_4,	/* MacBook Pro, 4th gen */
	M_MBP_5_1,	/* MacBook Pro, 5,1th gen */
	M_MBP_5_2,	/* MacBook Pro, 5,2th gen */
	M_MBP_5_3,	/* MacBook Pro, 5,3rd gen */
	M_MBP_6_1,	/* MacBook Pro, 6,1th gen */
	M_MBP_6_2,	/* MacBook Pro, 6,2th gen */
	M_MBP_7_1,	/* MacBook Pro, 7,1th gen */
	M_MBP_8_2,	/* MacBook Pro, 8,2nd gen */
	M_UNKNOWN	/* placeholder */
};

struct efifb_dmi_info {
	char *optname;
	unsigned long base;
	int stride;
	int width;
	int height;
	int flags;
};

#ifdef CONFIG_SYSFB

void sysfb_disable(struct device *dev);

bool sysfb_handles_screen_info(void);

#else /* CONFIG_SYSFB */

static inline void sysfb_disable(struct device *dev)
{
}

static inline bool sysfb_handles_screen_info(void)
{
	return false;
}

#endif /* CONFIG_SYSFB */

#ifdef CONFIG_EFI

extern struct efifb_dmi_info efifb_dmi_list[];
void sysfb_apply_efi_quirks(void);
void sysfb_set_efifb_fwnode(struct platform_device *pd);

#else /* CONFIG_EFI */

static inline void sysfb_apply_efi_quirks(void)
{
}

static inline void sysfb_set_efifb_fwnode(struct platform_device *pd)
{
}

#endif /* CONFIG_EFI */

#ifdef CONFIG_SYSFB_SIMPLEFB

bool sysfb_parse_mode(const struct screen_info *si,
		      struct simplefb_platform_data *mode);
struct platform_device *sysfb_create_simplefb(const struct screen_info *si,
					      const struct simplefb_platform_data *mode,
					      struct device *parent);

#else /* CONFIG_SYSFB_SIMPLE */

static inline bool sysfb_parse_mode(const struct screen_info *si,
				    struct simplefb_platform_data *mode)
{
	return false;
}

static inline struct platform_device *sysfb_create_simplefb(const struct screen_info *si,
							    const struct simplefb_platform_data *mode,
							    struct device *parent)
{
	return ERR_PTR(-EINVAL);
}

#endif /* CONFIG_SYSFB_SIMPLE */

#endif /* _LINUX_SYSFB_H */
