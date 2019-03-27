/*-
 * Copyright (c) 2014 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef DEV_FDT_PINCTRL_H
#define DEV_FDT_PINCTRL_H

#include "fdt_pinctrl_if.h"

/*
 * Configure pins by name or index.  This looks up the pinctrl-N property in
 * client's fdt data by index or name, and passes each handle in it to the
 * pinctrl driver for configuration.
 */
int fdt_pinctrl_configure(device_t client, u_int index);
int fdt_pinctrl_configure_by_name(device_t client, const char * name);

/*
 * Register a pinctrl driver so that it can be used by other devices which call
 * fdt_pinctrl_configure().  The pinprop argument is the name of a property that
 * identifies each descendent of the pinctrl node which is a pin configuration
 * node whose xref phandle can be passed to FDT_PINCTRL_CONFIGURE().  If this is
 * NULL, every descendant node is registered.
 */
int fdt_pinctrl_register(device_t pinctrl, const char *pinprop);

/*
 * Walk the device tree and configure pins for each enabled device whose
 * pinctrl-0 property contains references to nodes which are children of the
 * given pinctrl device.  This helper routine is for use by pinctrl drivers.
 */
int fdt_pinctrl_configure_tree(device_t pinctrl);

#endif /* DEV_FDT_PINCTRL_H */

