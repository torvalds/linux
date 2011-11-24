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

#ifndef __drivers_h__
#define __drivers_h__

#ifdef CONFIG_DRIVER_WEXT
extern struct wpa_driver_ops wpa_driver_wext_ops; /* driver_wext.c */
#endif /* CONFIG_DRIVER_WEXT */
#ifdef CONFIG_DRIVER_HOSTAP
extern struct wpa_driver_ops wpa_driver_hostap_ops; /* driver_hostap.c */
#endif /* CONFIG_DRIVER_HOSTAP */
#ifdef CONFIG_DRIVER_PRISM54
extern struct wpa_driver_ops wpa_driver_prism54_ops; /* driver_prism54.c */
#endif /* CONFIG_DRIVER_PRISM54 */
#ifdef CONFIG_DRIVER_HERMES
extern struct wpa_driver_ops wpa_driver_hermes_ops; /* driver_hermes.c */
#endif /* CONFIG_DRIVER_HERMES */
#ifdef CONFIG_DRIVER_MADWIFI
extern struct wpa_driver_ops wpa_driver_madwifi_ops; /* driver_madwifi.c */
#endif /* CONFIG_DRIVER_MADWIFI */
#ifdef CONFIG_DRIVER_ATMEL
extern struct wpa_driver_ops wpa_driver_atmel_ops; /* driver_atmel.c */
#endif /* CONFIG_DRIVER_ATMEL */
#ifdef CONFIG_DRIVER_NDISWRAPPER
/* driver_ndiswrapper.c */
extern struct wpa_driver_ops wpa_driver_ndiswrapper_ops;
#endif /* CONFIG_DRIVER_NDISWRAPPER */
#ifdef CONFIG_DRIVER_BROADCOM
extern struct wpa_driver_ops wpa_driver_broadcom_ops; /* driver_broadcom.c */
#endif /* CONFIG_DRIVER_BROADCOM */
#ifdef CONFIG_DRIVER_IPW
extern struct wpa_driver_ops wpa_driver_ipw_ops; /* driver_ipw.c */
#endif /* CONFIG_DRIVER_IPW */
#ifdef CONFIG_DRIVER_BSD
extern struct wpa_driver_ops wpa_driver_bsd_ops; /* driver_bsd.c */
#endif /* CONFIG_DRIVER_BSD */
#ifdef CONFIG_DRIVER_NDIS
extern struct wpa_driver_ops wpa_driver_ndis_ops; /* driver_ndis.c */
#endif /* CONFIG_DRIVER_NDIS */
#ifdef CONFIG_DRIVER_WIRED
extern struct wpa_driver_ops wpa_driver_wired_ops; /* driver_wired.c */
#endif /* CONFIG_DRIVER_WIRED */
#ifdef CONFIG_DRIVER_TEST
extern struct wpa_driver_ops wpa_driver_test_ops; /* driver_test.c */
#endif /* CONFIG_DRIVER_TEST */
#ifdef CONFIG_DRIVER_WIFIENGINE
extern const struct wpa_driver_ops wpa_driver_WE_ops; /* driver_wifiengine.c */
#endif /* CONFIG_DRIVER_WIFIENGINE */

#endif /* __drivers_h__ */
