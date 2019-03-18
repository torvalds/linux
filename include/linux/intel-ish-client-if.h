/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel ISH client Interface definitions
 *
 * Copyright (c) 2019, Intel Corporation.
 */

#ifndef _INTEL_ISH_CLIENT_IF_H_
#define _INTEL_ISH_CLIENT_IF_H_

struct ishtp_cl_device;

/* Get the device * from ishtp device instance */
struct device *ishtp_device(struct ishtp_cl_device *cl_device);
/* Trace interface for clients */
void *ishtp_trace_callback(struct ishtp_cl_device *cl_device);

#endif /* _INTEL_ISH_CLIENT_IF_H_ */
