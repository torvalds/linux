#!/bin/sh
# Runs hardware independent tests for i915 (drivers/gpu/drm/i915)

if ! /sbin/modprobe -q -r i915; then
	echo "drivers/gpu/i915: [SKIP]"
	exit 77
fi

if /sbin/modprobe -q i915 mock_selftests=-1; then
	echo "drivers/gpu/i915: ok"
else
	echo "drivers/gpu/i915: [FAIL]"
	exit 1
fi
