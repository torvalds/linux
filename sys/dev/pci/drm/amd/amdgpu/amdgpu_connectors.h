/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __AMDGPU_CONNECTORS_H__
#define __AMDGPU_CONNECTORS_H__

void amdgpu_connector_hotplug(struct drm_connector *connector);
int amdgpu_connector_get_monitor_bpc(struct drm_connector *connector);
u16 amdgpu_connector_encoder_get_dp_bridge_encoder_id(struct drm_connector *connector);
bool amdgpu_connector_is_dp12_capable(struct drm_connector *connector);
void
amdgpu_connector_add(struct amdgpu_device *adev,
		      uint32_t connector_id,
		      uint32_t supported_device,
		      int connector_type,
		      struct amdgpu_i2c_bus_rec *i2c_bus,
		      uint16_t connector_object_id,
		      struct amdgpu_hpd *hpd,
		      struct amdgpu_router *router);

#endif
