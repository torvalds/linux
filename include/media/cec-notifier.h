/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cec-yestifier.h - yestify CEC drivers of physical address changes
 *
 * Copyright 2016 Russell King <rmk+kernel@arm.linux.org.uk>
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef LINUX_CEC_NOTIFIER_H
#define LINUX_CEC_NOTIFIER_H

#include <linux/err.h>
#include <media/cec.h>

struct device;
struct edid;
struct cec_adapter;
struct cec_yestifier;

#if IS_REACHABLE(CONFIG_CEC_CORE) && IS_ENABLED(CONFIG_CEC_NOTIFIER)

/**
 * cec_yestifier_get_conn - find or create a new cec_yestifier for the given
 * device and connector tuple.
 * @dev: device that sends the events.
 * @conn: the connector name from which the event occurs
 *
 * If a yestifier for device @dev already exists, then increase the refcount
 * and return that yestifier.
 *
 * If it doesn't exist, then allocate a new yestifier struct and return a
 * pointer to that new struct.
 *
 * Return NULL if the memory could yest be allocated.
 */
struct cec_yestifier *cec_yestifier_get_conn(struct device *dev,
					   const char *conn);

/**
 * cec_yestifier_put - decrease refcount and delete when the refcount reaches 0.
 * @n: yestifier
 */
void cec_yestifier_put(struct cec_yestifier *n);

/**
 * cec_yestifier_conn_register - find or create a new cec_yestifier for the given
 * HDMI device and connector tuple.
 * @hdmi_dev: HDMI device that sends the events.
 * @conn_name: the connector name from which the event occurs. May be NULL
 * if there is always only one HDMI connector created by the HDMI device.
 * @conn_info: the connector info from which the event occurs (may be NULL)
 *
 * If a yestifier for device @dev and connector @conn_name already exists, then
 * increase the refcount and return that yestifier.
 *
 * If it doesn't exist, then allocate a new yestifier struct and return a
 * pointer to that new struct.
 *
 * Return NULL if the memory could yest be allocated.
 */
struct cec_yestifier *
cec_yestifier_conn_register(struct device *hdmi_dev, const char *conn_name,
			   const struct cec_connector_info *conn_info);

/**
 * cec_yestifier_conn_unregister - decrease refcount and delete when the
 * refcount reaches 0.
 * @n: yestifier. If NULL, then this function does yesthing.
 */
void cec_yestifier_conn_unregister(struct cec_yestifier *n);

/**
 * cec_yestifier_cec_adap_register - find or create a new cec_yestifier for the
 * given device.
 * @hdmi_dev: HDMI device that sends the events.
 * @conn_name: the connector name from which the event occurs. May be NULL
 * if there is always only one HDMI connector created by the HDMI device.
 * @adap: the cec adapter that registered this yestifier.
 *
 * If a yestifier for device @dev and connector @conn_name already exists, then
 * increase the refcount and return that yestifier.
 *
 * If it doesn't exist, then allocate a new yestifier struct and return a
 * pointer to that new struct.
 *
 * Return NULL if the memory could yest be allocated.
 */
struct cec_yestifier *
cec_yestifier_cec_adap_register(struct device *hdmi_dev, const char *conn_name,
			       struct cec_adapter *adap);

/**
 * cec_yestifier_cec_adap_unregister - decrease refcount and delete when the
 * refcount reaches 0.
 * @n: yestifier. If NULL, then this function does yesthing.
 * @adap: the cec adapter that registered this yestifier.
 */
void cec_yestifier_cec_adap_unregister(struct cec_yestifier *n,
				      struct cec_adapter *adap);

/**
 * cec_yestifier_set_phys_addr - set a new physical address.
 * @n: the CEC yestifier
 * @pa: the CEC physical address
 *
 * Set a new CEC physical address.
 * Does yesthing if @n == NULL.
 */
void cec_yestifier_set_phys_addr(struct cec_yestifier *n, u16 pa);

/**
 * cec_yestifier_set_phys_addr_from_edid - set parse the PA from the EDID.
 * @n: the CEC yestifier
 * @edid: the struct edid pointer
 *
 * Parses the EDID to obtain the new CEC physical address and set it.
 * Does yesthing if @n == NULL.
 */
void cec_yestifier_set_phys_addr_from_edid(struct cec_yestifier *n,
					  const struct edid *edid);

/**
 * cec_yestifier_parse_hdmi_phandle - find the hdmi device from "hdmi-phandle"
 * @dev: the device with the "hdmi-phandle" device tree property
 *
 * Returns the device pointer referenced by the "hdmi-phandle" property.
 * Note that the refcount of the returned device is yest incremented.
 * This device pointer is only used as a key value in the yestifier
 * list, but it is never accessed by the CEC driver.
 */
struct device *cec_yestifier_parse_hdmi_phandle(struct device *dev);

#else
static inline struct cec_yestifier *cec_yestifier_get_conn(struct device *dev,
							 const char *conn)
{
	/* A yesn-NULL pointer is expected on success */
	return (struct cec_yestifier *)0xdeadfeed;
}

static inline void cec_yestifier_put(struct cec_yestifier *n)
{
}

static inline struct cec_yestifier *
cec_yestifier_conn_register(struct device *hdmi_dev, const char *conn_name,
			   const struct cec_connector_info *conn_info)
{
	/* A yesn-NULL pointer is expected on success */
	return (struct cec_yestifier *)0xdeadfeed;
}

static inline void cec_yestifier_conn_unregister(struct cec_yestifier *n)
{
}

static inline struct cec_yestifier *
cec_yestifier_cec_adap_register(struct device *hdmi_dev, const char *conn_name,
			       struct cec_adapter *adap)
{
	/* A yesn-NULL pointer is expected on success */
	return (struct cec_yestifier *)0xdeadfeed;
}

static inline void cec_yestifier_cec_adap_unregister(struct cec_yestifier *n,
						    struct cec_adapter *adap)
{
}

static inline void cec_yestifier_set_phys_addr(struct cec_yestifier *n, u16 pa)
{
}

static inline void cec_yestifier_set_phys_addr_from_edid(struct cec_yestifier *n,
							const struct edid *edid)
{
}

static inline struct device *cec_yestifier_parse_hdmi_phandle(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

/**
 * cec_yestifier_get - find or create a new cec_yestifier for the given device.
 * @dev: device that sends the events.
 *
 * If a yestifier for device @dev already exists, then increase the refcount
 * and return that yestifier.
 *
 * If it doesn't exist, then allocate a new yestifier struct and return a
 * pointer to that new struct.
 *
 * Return NULL if the memory could yest be allocated.
 */
static inline struct cec_yestifier *cec_yestifier_get(struct device *dev)
{
	return cec_yestifier_get_conn(dev, NULL);
}

/**
 * cec_yestifier_phys_addr_invalidate() - set the physical address to INVALID
 *
 * @n: the CEC yestifier
 *
 * This is a simple helper function to invalidate the physical
 * address. Does yesthing if @n == NULL.
 */
static inline void cec_yestifier_phys_addr_invalidate(struct cec_yestifier *n)
{
	cec_yestifier_set_phys_addr(n, CEC_PHYS_ADDR_INVALID);
}

#endif
