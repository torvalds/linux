/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCREEN_INFO_H
#define _SCREEN_INFO_H

#include <uapi/linux/screen_info.h>

#include <linux/bits.h>

/**
 * SCREEN_INFO_MAX_RESOURCES - maximum number of resources per screen_info
 */
#define SCREEN_INFO_MAX_RESOURCES	3

struct pci_dev;
struct resource;

static inline bool __screen_info_has_lfb(unsigned int type)
{
	return (type == VIDEO_TYPE_VLFB) || (type == VIDEO_TYPE_EFI);
}

static inline u64 __screen_info_lfb_base(const struct screen_info *si)
{
	u64 lfb_base = si->lfb_base;

	if (si->capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		lfb_base |= (u64)si->ext_lfb_base << 32;

	return lfb_base;
}

static inline void __screen_info_set_lfb_base(struct screen_info *si, u64 lfb_base)
{
	si->lfb_base = lfb_base & GENMASK_ULL(31, 0);
	si->ext_lfb_base = (lfb_base & GENMASK_ULL(63, 32)) >> 32;

	if (si->ext_lfb_base)
		si->capabilities |= VIDEO_CAPABILITY_64BIT_BASE;
	else
		si->capabilities &= ~VIDEO_CAPABILITY_64BIT_BASE;
}

static inline u64 __screen_info_lfb_size(const struct screen_info *si, unsigned int type)
{
	u64 lfb_size = si->lfb_size;

	if (type == VIDEO_TYPE_VLFB)
		lfb_size <<= 16;
	return lfb_size;
}

static inline unsigned int __screen_info_video_type(unsigned int type)
{
	switch (type) {
	case VIDEO_TYPE_MDA:
	case VIDEO_TYPE_CGA:
	case VIDEO_TYPE_EGAM:
	case VIDEO_TYPE_EGAC:
	case VIDEO_TYPE_VGAC:
	case VIDEO_TYPE_VLFB:
	case VIDEO_TYPE_PICA_S3:
	case VIDEO_TYPE_MIPS_G364:
	case VIDEO_TYPE_SGI:
	case VIDEO_TYPE_TGAC:
	case VIDEO_TYPE_SUN:
	case VIDEO_TYPE_SUNPCI:
	case VIDEO_TYPE_PMAC:
	case VIDEO_TYPE_EFI:
		return type;
	default:
		return 0;
	}
}

/**
 * screen_info_video_type() - Decodes the video type from struct screen_info
 * @si: an instance of struct screen_info
 *
 * Returns:
 * A VIDEO_TYPE_ constant representing si's type of video display, or 0 otherwise.
 */
static inline unsigned int screen_info_video_type(const struct screen_info *si)
{
	unsigned int type;

	// check if display output is on
	if (!si->orig_video_isVGA)
		return 0;

	// check for a known VIDEO_TYPE_ constant
	type = __screen_info_video_type(si->orig_video_isVGA);
	if (type)
		return si->orig_video_isVGA;

	// check if text mode has been initialized
	if (!si->orig_video_lines || !si->orig_video_cols)
		return 0;

	// 80x25 text, mono
	if (si->orig_video_mode == 0x07) {
		if ((si->orig_video_ega_bx & 0xff) != 0x10)
			return VIDEO_TYPE_EGAM;
		else
			return VIDEO_TYPE_MDA;
	}

	// EGA/VGA, 16 colors
	if ((si->orig_video_ega_bx & 0xff) != 0x10) {
		if (si->orig_video_isVGA)
			return VIDEO_TYPE_VGAC;
		else
			return VIDEO_TYPE_EGAC;
	}

	// the rest...
	return VIDEO_TYPE_CGA;
}

ssize_t screen_info_resources(const struct screen_info *si, struct resource *r, size_t num);

#if defined(CONFIG_PCI)
void screen_info_apply_fixups(void);
struct pci_dev *screen_info_pci_dev(const struct screen_info *si);
#else
static inline void screen_info_apply_fixups(void)
{ }
static inline struct pci_dev *screen_info_pci_dev(const struct screen_info *si)
{
	return NULL;
}
#endif

extern struct screen_info screen_info;

#endif /* _SCREEN_INFO_H */
