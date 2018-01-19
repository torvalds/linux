#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Runs API tests for struct drm_mm (DRM range manager)

if ! /sbin/modprobe -n -q test-drm_mm; then
       echo "drivers/gpu/drm_mm: [skip]"
       exit 77
fi

if /sbin/modprobe -q test-drm_mm; then
       /sbin/modprobe -q -r test-drm_mm
       echo "drivers/gpu/drm_mm: ok"
else
       echo "drivers/gpu/drm_mm: [FAIL]"
       exit 1
fi
