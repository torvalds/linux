// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//

/*
 * Management of HDaudio multi-link (capabilities, power, coupling)
 */

#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>
#include <sound/hda-mlink.h>

#include <linux/bitfield.h>
#include <linux/module.h>

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_MLINK)

/* worst-case number of sublinks is used for sublink refcount array allocation only */
#define HDAML_MAX_SUBLINKS (AZX_ML_LCTL_CPA_SHIFT - AZX_ML_LCTL_SPA_SHIFT)

/**
 * struct hdac_ext2_link - HDAudio extended+alternate link
 *
 * @hext_link:		hdac_ext_link
 * @alt:		flag set for alternate extended links
 * @intc:		boolean for interrupt capable
 * @ofls:		boolean for offload support
 * @lss:		boolean for link synchronization capabilities
 * @slcount:		sublink count
 * @elid:		extended link ID (AZX_REG_ML_LEPTR_ID_ defines)
 * @elver:		extended link version
 * @leptr:		extended link pointer
 * @eml_lock:		mutual exclusion to access shared registers e.g. CPA/SPA bits
 * in LCTL register
 * @sublink_ref_count:	array of refcounts, required to power-manage sublinks independently
 * @base_ptr:		pointer to shim/ip/shim_vs space
 * @instance_offset:	offset between each of @slcount instances managed by link
 * @shim_offset:	offset to SHIM register base
 * @ip_offset:		offset to IP register base
 * @shim_vs_offset:	offset to vendor-specific (VS) SHIM base
 */
struct hdac_ext2_link {
	struct hdac_ext_link hext_link;

	/* read directly from LCAP register */
	bool alt;
	bool intc;
	bool ofls;
	bool lss;
	int slcount;
	int elid;
	int elver;
	u32 leptr;

	struct mutex eml_lock; /* prevent concurrent access to e.g. CPA/SPA */
	int sublink_ref_count[HDAML_MAX_SUBLINKS];

	/* internal values computed from LCAP contents */
	void __iomem *base_ptr;
	u32 instance_offset;
	u32 shim_offset;
	u32 ip_offset;
	u32 shim_vs_offset;
};

#define hdac_ext_link_to_ext2(h) container_of(h, struct hdac_ext2_link, hext_link)

#define AZX_REG_SDW_INSTANCE_OFFSET			0x8000
#define AZX_REG_SDW_SHIM_OFFSET				0x0
#define AZX_REG_SDW_IP_OFFSET				0x100
#define AZX_REG_SDW_VS_SHIM_OFFSET			0x6000

/* only one instance supported */
#define AZX_REG_INTEL_DMIC_SHIM_OFFSET			0x0
#define AZX_REG_INTEL_DMIC_IP_OFFSET			0x100
#define AZX_REG_INTEL_DMIC_VS_SHIM_OFFSET		0x6000

#define AZX_REG_INTEL_SSP_INSTANCE_OFFSET		0x1000
#define AZX_REG_INTEL_SSP_SHIM_OFFSET			0x0
#define AZX_REG_INTEL_SSP_IP_OFFSET			0x100
#define AZX_REG_INTEL_SSP_VS_SHIM_OFFSET		0xC00

/* only one instance supported */
#define AZX_REG_INTEL_UAOL_SHIM_OFFSET			0x0
#define AZX_REG_INTEL_UAOL_IP_OFFSET			0x100
#define AZX_REG_INTEL_UAOL_VS_SHIM_OFFSET		0xC00

/* HDAML section - this part follows sequences in the hardware specification,
 * including naming conventions and the use of the hdaml_ prefix.
 * The code is intentionally minimal with limited dependencies on frameworks or
 * helpers. Locking and scanning lists is handled at a higher level
 */

static int hdaml_lnk_enum(struct device *dev, struct hdac_ext2_link *h2link,
			  void __iomem *remap_addr, void __iomem *ml_addr, int link_idx)
{
	struct hdac_ext_link *hlink = &h2link->hext_link;
	u32 base_offset;

	hlink->lcaps  = readl(ml_addr + AZX_REG_ML_LCAP);

	h2link->alt = FIELD_GET(AZX_ML_HDA_LCAP_ALT, hlink->lcaps);

	/* handle alternate extensions */
	if (!h2link->alt) {
		h2link->slcount = 1;

		/*
		 * LSDIID is initialized by hardware for HDaudio link,
		 * it needs to be setup by software for alternate links
		 */
		hlink->lsdiid = readw(ml_addr + AZX_REG_ML_LSDIID);

		dev_dbg(dev, "Link %d: HDAudio - lsdiid=%d\n",
			link_idx, hlink->lsdiid);

		return 0;
	}

	h2link->intc = FIELD_GET(AZX_ML_HDA_LCAP_INTC, hlink->lcaps);
	h2link->ofls = FIELD_GET(AZX_ML_HDA_LCAP_OFLS, hlink->lcaps);
	h2link->lss = FIELD_GET(AZX_ML_HDA_LCAP_LSS, hlink->lcaps);

	/* read slcount (increment due to zero-based hardware representation */
	h2link->slcount = FIELD_GET(AZX_ML_HDA_LCAP_SLCOUNT, hlink->lcaps) + 1;
	dev_dbg(dev, "Link %d: HDAudio extended - sublink count %d\n",
		link_idx, h2link->slcount);

	/* find IP ID and offsets */
	h2link->leptr = readl(hlink->ml_addr + AZX_REG_ML_LEPTR);

	h2link->elid = FIELD_GET(AZX_REG_ML_LEPTR_ID, h2link->leptr);

	base_offset = FIELD_GET(AZX_REG_ML_LEPTR_PTR, h2link->leptr);
	h2link->base_ptr = remap_addr + base_offset;

	switch (h2link->elid) {
	case AZX_REG_ML_LEPTR_ID_SDW:
		h2link->shim_offset = AZX_REG_SDW_SHIM_OFFSET;
		h2link->ip_offset = AZX_REG_SDW_IP_OFFSET;
		h2link->shim_vs_offset = AZX_REG_SDW_VS_SHIM_OFFSET;
		dev_dbg(dev, "Link %d: HDAudio extended - SoundWire alternate link, leptr.ptr %#x\n",
			link_idx, base_offset);
		break;
	case AZX_REG_ML_LEPTR_ID_INTEL_DMIC:
		h2link->shim_offset = AZX_REG_INTEL_DMIC_SHIM_OFFSET;
		h2link->ip_offset = AZX_REG_INTEL_DMIC_IP_OFFSET;
		h2link->shim_vs_offset = AZX_REG_INTEL_DMIC_VS_SHIM_OFFSET;
		dev_dbg(dev, "Link %d: HDAudio extended - INTEL DMIC alternate link, leptr.ptr %#x\n",
			link_idx, base_offset);
		break;
	case AZX_REG_ML_LEPTR_ID_INTEL_SSP:
		h2link->shim_offset = AZX_REG_INTEL_SSP_SHIM_OFFSET;
		h2link->ip_offset = AZX_REG_INTEL_SSP_IP_OFFSET;
		h2link->shim_vs_offset = AZX_REG_INTEL_SSP_VS_SHIM_OFFSET;
		dev_dbg(dev, "Link %d: HDAudio extended - INTEL SSP alternate link, leptr.ptr %#x\n",
			link_idx, base_offset);
		break;
	case AZX_REG_ML_LEPTR_ID_INTEL_UAOL:
		h2link->shim_offset = AZX_REG_INTEL_UAOL_SHIM_OFFSET;
		h2link->ip_offset = AZX_REG_INTEL_UAOL_IP_OFFSET;
		h2link->shim_vs_offset = AZX_REG_INTEL_UAOL_VS_SHIM_OFFSET;
		dev_dbg(dev, "Link %d: HDAudio extended - INTEL UAOL alternate link, leptr.ptr %#x\n",
			link_idx, base_offset);
		break;
	default:
		dev_err(dev, "Link %d: HDAudio extended - Unsupported alternate link, leptr.id=%#02x value\n",
			link_idx, h2link->elid);
		return -EINVAL;
	}
	return 0;
}

/*
 * Hardware recommendations are to wait ~10us before checking any hardware transition
 * reported by bits changing status.
 * This value does not need to be super-precise, a slack of 5us is perfectly acceptable.
 * The worst-case is about 1ms before reporting an issue
 */
#define HDAML_POLL_DELAY_MIN_US 10
#define HDAML_POLL_DELAY_SLACK_US 5
#define HDAML_POLL_DELAY_RETRY  100

static int check_sublink_power(u32 __iomem *lctl, int sublink, bool enabled)
{
	int mask = BIT(sublink) << AZX_ML_LCTL_CPA_SHIFT;
	int retry = HDAML_POLL_DELAY_RETRY;
	u32 val;

	usleep_range(HDAML_POLL_DELAY_MIN_US,
		     HDAML_POLL_DELAY_MIN_US + HDAML_POLL_DELAY_SLACK_US);
	do {
		val = readl(lctl);
		if (enabled) {
			if (val & mask)
				return 0;
		} else {
			if (!(val & mask))
				return 0;
		}
		usleep_range(HDAML_POLL_DELAY_MIN_US,
			     HDAML_POLL_DELAY_MIN_US + HDAML_POLL_DELAY_SLACK_US);

	} while (--retry);

	return -EIO;
}

static int hdaml_link_init(u32 __iomem *lctl, int sublink)
{
	u32 val;
	u32 mask = BIT(sublink) << AZX_ML_LCTL_SPA_SHIFT;

	val = readl(lctl);
	val |= mask;

	writel(val, lctl);

	return check_sublink_power(lctl, sublink, true);
}

static int hdaml_link_shutdown(u32 __iomem *lctl, int sublink)
{
	u32 val;
	u32 mask;

	val = readl(lctl);
	mask = BIT(sublink) << AZX_ML_LCTL_SPA_SHIFT;
	val &= ~mask;

	writel(val, lctl);

	return check_sublink_power(lctl, sublink, false);
}

static void hdaml_link_enable_interrupt(u32 __iomem *lctl, bool enable)
{
	u32 val;

	val = readl(lctl);
	if (enable)
		val |= AZX_ML_LCTL_INTEN;
	else
		val &= ~AZX_ML_LCTL_INTEN;

	writel(val, lctl);
}

static bool hdaml_link_check_interrupt(u32 __iomem *lctl)
{
	u32 val;

	val = readl(lctl);

	return val & AZX_ML_LCTL_INTSTS;
}

static int hdaml_wait_bit(void __iomem *base, int offset, u32 mask, u32 target)
{
	int timeout = HDAML_POLL_DELAY_RETRY;
	u32 reg_read;

	do {
		reg_read = readl(base + offset);
		if ((reg_read & mask) == target)
			return 0;

		timeout--;
		usleep_range(HDAML_POLL_DELAY_MIN_US,
			     HDAML_POLL_DELAY_MIN_US + HDAML_POLL_DELAY_SLACK_US);
	} while (timeout != 0);

	return -EAGAIN;
}

static void hdaml_link_set_syncprd(u32 __iomem *lsync, u32 syncprd)
{
	u32 val;

	val = readl(lsync);
	val &= ~AZX_REG_ML_LSYNC_SYNCPRD;
	val |= (syncprd & AZX_REG_ML_LSYNC_SYNCPRD);

	/*
	 * set SYNCPU but do not wait. The bit is cleared by hardware when
	 * the link becomes active.
	 */
	val |= AZX_REG_ML_LSYNC_SYNCPU;

	writel(val, lsync);
}

static int hdaml_link_wait_syncpu(u32 __iomem *lsync)
{
	return hdaml_wait_bit(lsync, 0, AZX_REG_ML_LSYNC_SYNCPU, 0);
}

static void hdaml_link_sync_arm(u32 __iomem *lsync, int sublink)
{
	u32 val;

	val = readl(lsync);
	val |= (AZX_REG_ML_LSYNC_CMDSYNC << sublink);

	writel(val, lsync);
}

static void hdaml_link_sync_go(u32 __iomem *lsync)
{
	u32 val;

	val = readl(lsync);
	val |= AZX_REG_ML_LSYNC_SYNCGO;

	writel(val, lsync);
}

static bool hdaml_link_check_cmdsync(u32 __iomem *lsync, u32 cmdsync_mask)
{
	u32 val;

	val = readl(lsync);

	return !!(val & cmdsync_mask);
}

static void hdaml_link_set_lsdiid(u32 __iomem *lsdiid, int dev_num)
{
	u32 val;

	val = readl(lsdiid);
	val |= BIT(dev_num);

	writel(val, lsdiid);
}

static void hdaml_lctl_offload_enable(u32 __iomem *lctl, bool enable)
{
	u32 val = readl(lctl);

	if (enable)
		val |=  AZX_ML_LCTL_OFLEN;
	else
		val &=  ~AZX_ML_LCTL_OFLEN;

	writel(val, lctl);
}

/* END HDAML section */

static int hda_ml_alloc_h2link(struct hdac_bus *bus, int index)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;
	int ret;

	h2link  = kzalloc(sizeof(*h2link), GFP_KERNEL);
	if (!h2link)
		return -ENOMEM;

	/* basic initialization */
	hlink = &h2link->hext_link;

	hlink->index = index;
	hlink->bus = bus;
	hlink->ml_addr = bus->mlcap + AZX_ML_BASE + (AZX_ML_INTERVAL * index);

	ret = hdaml_lnk_enum(bus->dev, h2link, bus->remap_addr, hlink->ml_addr, index);
	if (ret < 0) {
		kfree(h2link);
		return ret;
	}

	mutex_init(&h2link->eml_lock);

	list_add_tail(&hlink->list, &bus->hlink_list);

	/*
	 * HDaudio regular links are powered-on by default, the
	 * refcount needs to be initialized.
	 */
	if (!h2link->alt)
		hlink->ref_count = 1;

	return 0;
}

int hda_bus_ml_init(struct hdac_bus *bus)
{
	u32 link_count;
	int ret;
	int i;

	if (!bus->mlcap)
		return 0;

	link_count = readl(bus->mlcap + AZX_REG_ML_MLCD) + 1;

	dev_dbg(bus->dev, "HDAudio Multi-Link count: %d\n", link_count);

	for (i = 0; i < link_count; i++) {
		ret = hda_ml_alloc_h2link(bus, i);
		if (ret < 0) {
			hda_bus_ml_free(bus);
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_NS(hda_bus_ml_init, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_free(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink, *_h;
	struct hdac_ext2_link *h2link;

	if (!bus->mlcap)
		return;

	list_for_each_entry_safe(hlink, _h, &bus->hlink_list, list) {
		list_del(&hlink->list);
		h2link = hdac_ext_link_to_ext2(hlink);

		mutex_destroy(&h2link->eml_lock);
		kfree(h2link);
	}
}
EXPORT_SYMBOL_NS(hda_bus_ml_free, SND_SOC_SOF_HDA_MLINK);

static struct hdac_ext2_link *
find_ext2_link(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext_link *hlink;

	list_for_each_entry(hlink, &bus->hlink_list, list) {
		struct hdac_ext2_link *h2link = hdac_ext_link_to_ext2(hlink);

		if (h2link->alt == alt && h2link->elid == elid)
			return h2link;
	}

	return NULL;
}

int hdac_bus_eml_get_count(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return 0;

	return h2link->slcount;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_get_count, SND_SOC_SOF_HDA_MLINK);

void hdac_bus_eml_enable_interrupt(struct hdac_bus *bus, bool alt, int elid, bool enable)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return;

	if (!h2link->intc)
		return;

	hlink = &h2link->hext_link;

	mutex_lock(&h2link->eml_lock);

	hdaml_link_enable_interrupt(hlink->ml_addr + AZX_REG_ML_LCTL, enable);

	mutex_unlock(&h2link->eml_lock);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_enable_interrupt, SND_SOC_SOF_HDA_MLINK);

bool hdac_bus_eml_check_interrupt(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return false;

	if (!h2link->intc)
		return false;

	hlink = &h2link->hext_link;

	return hdaml_link_check_interrupt(hlink->ml_addr + AZX_REG_ML_LCTL);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_check_interrupt, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_set_syncprd_unlocked(struct hdac_bus *bus, bool alt, int elid, u32 syncprd)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return 0;

	if (!h2link->lss)
		return 0;

	hlink = &h2link->hext_link;

	hdaml_link_set_syncprd(hlink->ml_addr + AZX_REG_ML_LSYNC, syncprd);

	return 0;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_set_syncprd_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_set_syncprd_unlocked(struct hdac_bus *bus, u32 syncprd)
{
	return hdac_bus_eml_set_syncprd_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW, syncprd);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_set_syncprd_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_wait_syncpu_unlocked(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return 0;

	if (!h2link->lss)
		return 0;

	hlink = &h2link->hext_link;

	return hdaml_link_wait_syncpu(hlink->ml_addr + AZX_REG_ML_LSYNC);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_wait_syncpu_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_wait_syncpu_unlocked(struct hdac_bus *bus)
{
	return hdac_bus_eml_wait_syncpu_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_wait_syncpu_unlocked, SND_SOC_SOF_HDA_MLINK);

void hdac_bus_eml_sync_arm_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return;

	if (!h2link->lss)
		return;

	hlink = &h2link->hext_link;

	hdaml_link_sync_arm(hlink->ml_addr + AZX_REG_ML_LSYNC, sublink);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sync_arm_unlocked, SND_SOC_SOF_HDA_MLINK);

void hdac_bus_eml_sdw_sync_arm_unlocked(struct hdac_bus *bus, int sublink)
{
	hdac_bus_eml_sync_arm_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW, sublink);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_sync_arm_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sync_go_unlocked(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return 0;

	if (!h2link->lss)
		return 0;

	hlink = &h2link->hext_link;

	hdaml_link_sync_go(hlink->ml_addr + AZX_REG_ML_LSYNC);

	return 0;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sync_go_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_sync_go_unlocked(struct hdac_bus *bus)
{
	return hdac_bus_eml_sync_go_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_sync_go_unlocked, SND_SOC_SOF_HDA_MLINK);

bool hdac_bus_eml_check_cmdsync_unlocked(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;
	u32 cmdsync_mask;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return 0;

	if (!h2link->lss)
		return 0;

	hlink = &h2link->hext_link;

	cmdsync_mask = GENMASK(AZX_REG_ML_LSYNC_CMDSYNC_SHIFT + h2link->slcount - 1,
			       AZX_REG_ML_LSYNC_CMDSYNC_SHIFT);

	return hdaml_link_check_cmdsync(hlink->ml_addr + AZX_REG_ML_LSYNC,
					cmdsync_mask);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_check_cmdsync_unlocked, SND_SOC_SOF_HDA_MLINK);

bool hdac_bus_eml_sdw_check_cmdsync_unlocked(struct hdac_bus *bus)
{
	return hdac_bus_eml_check_cmdsync_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_check_cmdsync_unlocked, SND_SOC_SOF_HDA_MLINK);

static int hdac_bus_eml_power_up_base(struct hdac_bus *bus, bool alt, int elid, int sublink,
				      bool eml_lock)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;
	int ret = 0;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return -ENODEV;

	if (sublink >= h2link->slcount)
		return -EINVAL;

	hlink = &h2link->hext_link;

	if (eml_lock)
		mutex_lock(&h2link->eml_lock);

	if (!alt) {
		if (++hlink->ref_count > 1)
			goto skip_init;
	} else {
		if (++h2link->sublink_ref_count[sublink] > 1)
			goto skip_init;
	}

	ret = hdaml_link_init(hlink->ml_addr + AZX_REG_ML_LCTL, sublink);

skip_init:
	if (eml_lock)
		mutex_unlock(&h2link->eml_lock);

	return ret;
}

int hdac_bus_eml_power_up(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return hdac_bus_eml_power_up_base(bus, alt, elid, sublink, true);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_power_up, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_power_up_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return hdac_bus_eml_power_up_base(bus, alt, elid, sublink, false);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_power_up_unlocked, SND_SOC_SOF_HDA_MLINK);

static int hdac_bus_eml_power_down_base(struct hdac_bus *bus, bool alt, int elid, int sublink,
					bool eml_lock)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;
	int ret = 0;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return -ENODEV;

	if (sublink >= h2link->slcount)
		return -EINVAL;

	hlink = &h2link->hext_link;

	if (eml_lock)
		mutex_lock(&h2link->eml_lock);

	if (!alt) {
		if (--hlink->ref_count > 0)
			goto skip_shutdown;
	} else {
		if (--h2link->sublink_ref_count[sublink] > 0)
			goto skip_shutdown;
	}
	ret = hdaml_link_shutdown(hlink->ml_addr + AZX_REG_ML_LCTL, sublink);

skip_shutdown:
	if (eml_lock)
		mutex_unlock(&h2link->eml_lock);

	return ret;
}

int hdac_bus_eml_power_down(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return hdac_bus_eml_power_down_base(bus, alt, elid, sublink, true);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_power_down, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_power_down_unlocked(struct hdac_bus *bus, bool alt, int elid, int sublink)
{
	return hdac_bus_eml_power_down_base(bus, alt, elid, sublink, false);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_power_down_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_power_up_unlocked(struct hdac_bus *bus, int sublink)
{
	return hdac_bus_eml_power_up_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW, sublink);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_power_up_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_power_down_unlocked(struct hdac_bus *bus, int sublink)
{
	return hdac_bus_eml_power_down_unlocked(bus, true, AZX_REG_ML_LEPTR_ID_SDW, sublink);
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_power_down_unlocked, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_sdw_set_lsdiid(struct hdac_bus *bus, int sublink, int dev_num)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, true, AZX_REG_ML_LEPTR_ID_SDW);
	if (!h2link)
		return -ENODEV;

	hlink = &h2link->hext_link;

	mutex_lock(&h2link->eml_lock);

	hdaml_link_set_lsdiid(hlink->ml_addr + AZX_REG_ML_LSDIID_OFFSET(sublink), dev_num);

	mutex_unlock(&h2link->eml_lock);

	return 0;
} EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_set_lsdiid, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_put_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	list_for_each_entry(hlink, &bus->hlink_list, list) {
		struct hdac_ext2_link *h2link = hdac_ext_link_to_ext2(hlink);

		if (!h2link->alt)
			snd_hdac_ext_bus_link_put(bus, hlink);
	}
}
EXPORT_SYMBOL_NS(hda_bus_ml_put_all, SND_SOC_SOF_HDA_MLINK);

void hda_bus_ml_reset_losidv(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	/* Reset stream-to-link mapping */
	list_for_each_entry(hlink, &bus->hlink_list, list)
		writel(0, hlink->ml_addr + AZX_REG_ML_LOSIDV);
}
EXPORT_SYMBOL_NS(hda_bus_ml_reset_losidv, SND_SOC_SOF_HDA_MLINK);

int hda_bus_ml_resume(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;
	int ret;

	/* power up links that were active before suspend */
	list_for_each_entry(hlink, &bus->hlink_list, list) {
		struct hdac_ext2_link *h2link = hdac_ext_link_to_ext2(hlink);

		if (!h2link->alt && hlink->ref_count) {
			ret = snd_hdac_ext_bus_link_power_up(hlink);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_NS(hda_bus_ml_resume, SND_SOC_SOF_HDA_MLINK);

int hda_bus_ml_suspend(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;
	int ret;

	list_for_each_entry(hlink, &bus->hlink_list, list) {
		struct hdac_ext2_link *h2link = hdac_ext_link_to_ext2(hlink);

		if (!h2link->alt) {
			ret = snd_hdac_ext_bus_link_power_down(hlink);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_NS(hda_bus_ml_suspend, SND_SOC_SOF_HDA_MLINK);

struct mutex *hdac_bus_eml_get_mutex(struct hdac_bus *bus, bool alt, int elid)
{
	struct hdac_ext2_link *h2link;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return NULL;

	return &h2link->eml_lock;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_get_mutex, SND_SOC_SOF_HDA_MLINK);

struct hdac_ext_link *hdac_bus_eml_ssp_get_hlink(struct hdac_bus *bus)
{
	struct hdac_ext2_link *h2link;

	h2link = find_ext2_link(bus, true, AZX_REG_ML_LEPTR_ID_INTEL_SSP);
	if (!h2link)
		return NULL;

	return &h2link->hext_link;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_ssp_get_hlink, SND_SOC_SOF_HDA_MLINK);

struct hdac_ext_link *hdac_bus_eml_dmic_get_hlink(struct hdac_bus *bus)
{
	struct hdac_ext2_link *h2link;

	h2link = find_ext2_link(bus, true, AZX_REG_ML_LEPTR_ID_INTEL_DMIC);
	if (!h2link)
		return NULL;

	return &h2link->hext_link;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_dmic_get_hlink, SND_SOC_SOF_HDA_MLINK);

struct hdac_ext_link *hdac_bus_eml_sdw_get_hlink(struct hdac_bus *bus)
{
	struct hdac_ext2_link *h2link;

	h2link = find_ext2_link(bus, true, AZX_REG_ML_LEPTR_ID_SDW);
	if (!h2link)
		return NULL;

	return &h2link->hext_link;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_sdw_get_hlink, SND_SOC_SOF_HDA_MLINK);

int hdac_bus_eml_enable_offload(struct hdac_bus *bus, bool alt, int elid, bool enable)
{
	struct hdac_ext2_link *h2link;
	struct hdac_ext_link *hlink;

	h2link = find_ext2_link(bus, alt, elid);
	if (!h2link)
		return -ENODEV;

	if (!h2link->ofls)
		return 0;

	hlink = &h2link->hext_link;

	mutex_lock(&h2link->eml_lock);

	hdaml_lctl_offload_enable(hlink->ml_addr + AZX_REG_ML_LCTL, enable);

	mutex_unlock(&h2link->eml_lock);

	return 0;
}
EXPORT_SYMBOL_NS(hdac_bus_eml_enable_offload, SND_SOC_SOF_HDA_MLINK);

#endif

MODULE_LICENSE("Dual BSD/GPL");
