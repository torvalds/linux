/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2023-2024 Intel Corporation
 *
 * Authors: Cezary Rojewski <cezary.rojewski@intel.com>
 *          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
 */

#ifndef __ACPI_NHLT_H__
#define __ACPI_NHLT_H__

#include <linux/acpi.h>
#include <linux/kconfig.h>
#include <linux/overflow.h>
#include <linux/types.h>

#define __acpi_nhlt_endpoint_config(ep)		((void *)((ep) + 1))
#define __acpi_nhlt_config_caps(cfg)		((void *)((cfg) + 1))

/**
 * acpi_nhlt_endpoint_fmtscfg - Get the formats configuration space.
 * @ep:		the endpoint to retrieve the space for.
 *
 * Return: A pointer to the formats configuration space.
 */
static inline struct acpi_nhlt_formats_config *
acpi_nhlt_endpoint_fmtscfg(const struct acpi_nhlt_endpoint *ep)
{
	struct acpi_nhlt_config *cfg = __acpi_nhlt_endpoint_config(ep);

	return (struct acpi_nhlt_formats_config *)((u8 *)(cfg + 1) + cfg->capabilities_size);
}

#define __acpi_nhlt_first_endpoint(tb) \
	((void *)(tb + 1))

#define __acpi_nhlt_next_endpoint(ep) \
	((void *)((u8 *)(ep) + (ep)->length))

#define __acpi_nhlt_get_endpoint(tb, ep, i) \
	((i) ? __acpi_nhlt_next_endpoint(ep) : __acpi_nhlt_first_endpoint(tb))

#define __acpi_nhlt_first_fmtcfg(fmts) \
	((void *)(fmts + 1))

#define __acpi_nhlt_next_fmtcfg(fmt) \
	((void *)((u8 *)((fmt) + 1) + (fmt)->config.capabilities_size))

#define __acpi_nhlt_get_fmtcfg(fmts, fmt, i) \
	((i) ? __acpi_nhlt_next_fmtcfg(fmt) : __acpi_nhlt_first_fmtcfg(fmts))

/*
 * The for_each_nhlt_*() macros rely on an iterator to deal with the
 * variable length of each endpoint structure and the possible presence
 * of an OED-Config used by Windows only.
 */

/**
 * for_each_nhlt_endpoint - Iterate over endpoints in a NHLT table.
 * @tb:		the pointer to a NHLT table.
 * @ep:		the pointer to endpoint to use as loop cursor.
 */
#define for_each_nhlt_endpoint(tb, ep)					\
	for (unsigned int __i = 0;					\
	     __i < (tb)->endpoints_count &&				\
		(ep = __acpi_nhlt_get_endpoint(tb, ep, __i));		\
	     __i++)

/**
 * for_each_nhlt_fmtcfg - Iterate over format configurations.
 * @fmts:	the pointer to formats configuration space.
 * @fmt:	the pointer to format to use as loop cursor.
 */
#define for_each_nhlt_fmtcfg(fmts, fmt)					\
	for (unsigned int __i = 0;					\
	     __i < (fmts)->formats_count &&				\
		(fmt = __acpi_nhlt_get_fmtcfg(fmts, fmt, __i));	\
	     __i++)

/**
 * for_each_nhlt_endpoint_fmtcfg - Iterate over format configurations in an endpoint.
 * @ep:		the pointer to an endpoint.
 * @fmt:	the pointer to format to use as loop cursor.
 */
#define for_each_nhlt_endpoint_fmtcfg(ep, fmt) \
	for_each_nhlt_fmtcfg(acpi_nhlt_endpoint_fmtscfg(ep), fmt)

#if IS_ENABLED(CONFIG_ACPI_NHLT)

/*
 * System-wide pointer to the first NHLT table.
 *
 * A sound driver may utilize acpi_nhlt_get/put_gbl_table() on its
 * initialization and removal respectively to avoid excessive mapping
 * and unmapping of the memory occupied by the table between streaming
 * operations.
 */

acpi_status acpi_nhlt_get_gbl_table(void);
void acpi_nhlt_put_gbl_table(void);

bool acpi_nhlt_endpoint_match(const struct acpi_nhlt_endpoint *ep,
			      int link_type, int dev_type, int dir, int bus_id);
struct acpi_nhlt_endpoint *
acpi_nhlt_tb_find_endpoint(const struct acpi_table_nhlt *tb,
			   int link_type, int dev_type, int dir, int bus_id);
struct acpi_nhlt_endpoint *
acpi_nhlt_find_endpoint(int link_type, int dev_type, int dir, int bus_id);
struct acpi_nhlt_format_config *
acpi_nhlt_endpoint_find_fmtcfg(const struct acpi_nhlt_endpoint *ep,
			       u16 ch, u32 rate, u16 vbps, u16 bps);
struct acpi_nhlt_format_config *
acpi_nhlt_tb_find_fmtcfg(const struct acpi_table_nhlt *tb,
			 int link_type, int dev_type, int dir, int bus_id,
			 u16 ch, u32 rate, u16 vpbs, u16 bps);
struct acpi_nhlt_format_config *
acpi_nhlt_find_fmtcfg(int link_type, int dev_type, int dir, int bus_id,
		      u16 ch, u32 rate, u16 vpbs, u16 bps);
int acpi_nhlt_endpoint_mic_count(const struct acpi_nhlt_endpoint *ep);

#else /* !CONFIG_ACPI_NHLT */

static inline acpi_status acpi_nhlt_get_gbl_table(void)
{
	return AE_NOT_FOUND;
}

static inline void acpi_nhlt_put_gbl_table(void)
{
}

static inline bool
acpi_nhlt_endpoint_match(const struct acpi_nhlt_endpoint *ep,
			 int link_type, int dev_type, int dir, int bus_id)
{
	return false;
}

static inline struct acpi_nhlt_endpoint *
acpi_nhlt_tb_find_endpoint(const struct acpi_table_nhlt *tb,
			   int link_type, int dev_type, int dir, int bus_id)
{
	return NULL;
}

static inline struct acpi_nhlt_format_config *
acpi_nhlt_endpoint_find_fmtcfg(const struct acpi_nhlt_endpoint *ep,
			       u16 ch, u32 rate, u16 vbps, u16 bps)
{
	return NULL;
}

static inline struct acpi_nhlt_format_config *
acpi_nhlt_tb_find_fmtcfg(const struct acpi_table_nhlt *tb,
			 int link_type, int dev_type, int dir, int bus_id,
			 u16 ch, u32 rate, u16 vpbs, u16 bps)
{
	return NULL;
}

static inline int acpi_nhlt_endpoint_mic_count(const struct acpi_nhlt_endpoint *ep)
{
	return 0;
}

static inline struct acpi_nhlt_endpoint *
acpi_nhlt_find_endpoint(int link_type, int dev_type, int dir, int bus_id)
{
	return NULL;
}

static inline struct acpi_nhlt_format_config *
acpi_nhlt_find_fmtcfg(int link_type, int dev_type, int dir, int bus_id,
		      u16 ch, u32 rate, u16 vpbs, u16 bps)
{
	return NULL;
}

#endif /* CONFIG_ACPI_NHLT */

#endif /* __ACPI_NHLT_H__ */
