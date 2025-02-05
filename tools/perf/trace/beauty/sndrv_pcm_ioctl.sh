#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && beauty_uapi_sound_dir=$1 || beauty_uapi_sound_dir=tools/perf/trace/beauty/include/uapi/sound/

printf "static const char *sndrv_pcm_ioctl_cmds[] = {\n"
grep "^#define[\t ]\+SNDRV_PCM_IOCTL_" $beauty_uapi_sound_dir/asound.h | \
	sed -r 's/^#define +SNDRV_PCM_IOCTL_([A-Z0-9_]+)[\t ]+_IO[RW]*\( *.A., *(0x[[:xdigit:]]+),?.*/\t[\2] = \"\1\",/g'
printf "};\n"
