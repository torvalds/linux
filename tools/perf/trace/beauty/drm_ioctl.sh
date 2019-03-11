#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/drm/

printf "#ifndef DRM_COMMAND_BASE\n"
grep "#define DRM_COMMAND_BASE" $header_dir/drm.h
printf "#endif\n"

printf "static const char *drm_ioctl_cmds[] = {\n"
grep "^#define DRM_IOCTL.*DRM_IO" $header_dir/drm.h | \
	sed -r 's/^#define +DRM_IOCTL_([A-Z0-9_]+)[	 ]+DRM_IO[A-Z]* *\( *(0x[[:xdigit:]]+),*.*/	[\2] = "\1",/g'
grep "^#define DRM_I915_[A-Z_0-9]\+[	 ]\+0x" $header_dir/i915_drm.h | \
	sed -r 's/^#define +DRM_I915_([A-Z0-9_]+)[	 ]+(0x[[:xdigit:]]+)/\t[DRM_COMMAND_BASE + \2] = "I915_\1",/g'
printf "};\n"
