/*
 * WPA Supplicant / driver interface list
 * Copyright (c) 2004-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "drivers.h"

const struct wpa_driver_ops *wpa_supplicant_drivers[] =
{
#ifdef CONFIG_DRIVER_WEXT
	&wpa_driver_wext_ops,
#endif /* CONFIG_DRIVER_WEXT */
#ifdef CONFIG_DRIVER_HOSTAP
	&wpa_driver_hostap_ops,
#endif /* CONFIG_DRIVER_HOSTAP */
#ifdef CONFIG_DRIVER_PRISM54
	&wpa_driver_prism54_ops,
#endif /* CONFIG_DRIVER_PRISM54 */
#ifdef CONFIG_DRIVER_HERMES
	&wpa_driver_hermes_ops,
#endif /* CONFIG_DRIVER_HERMES */
#ifdef CONFIG_DRIVER_MADWIFI
	&wpa_driver_madwifi_ops,
#endif /* CONFIG_DRIVER_MADWIFI */
#ifdef CONFIG_DRIVER_ATMEL
	&wpa_driver_atmel_ops,
#endif /* CONFIG_DRIVER_ATMEL */
#ifdef CONFIG_DRIVER_NDISWRAPPER
	&wpa_driver_ndiswrapper_ops,
#endif /* CONFIG_DRIVER_NDISWRAPPER */
#ifdef CONFIG_DRIVER_BROADCOM
	&wpa_driver_broadcom_ops,
#endif /* CONFIG_DRIVER_BROADCOM */
#ifdef CONFIG_DRIVER_IPW
	&wpa_driver_ipw_ops,
#endif /* CONFIG_DRIVER_IPW */
#ifdef CONFIG_DRIVER_BSD
	&wpa_driver_bsd_ops,
#endif /* CONFIG_DRIVER_BSD */
#ifdef CONFIG_DRIVER_NDIS
	&wpa_driver_ndis_ops,
#endif /* CONFIG_DRIVER_NDIS */
#ifdef CONFIG_DRIVER_WIRED
	&wpa_driver_wired_ops,
#endif /* CONFIG_DRIVER_WIRED */
#ifdef CONFIG_DRIVER_TEST
	&wpa_driver_test_ops,
#endif /* CONFIG_DRIVER_TEST */
#ifdef CONFIG_DRIVER_WIFIENGINE
	&wpa_driver_WE_ops,
#endif /* CONFIG_DRIVER_WIFIENGINE */
	NULL
};
