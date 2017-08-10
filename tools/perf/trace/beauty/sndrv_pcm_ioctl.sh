#!/bin/sh

sound_header_dir=$1

printf "static const char *sndrv_pcm_ioctl_cmds[] = {\n"
grep "^#define[\t ]\+SNDRV_PCM_IOCTL_" $sound_header_dir/asound.h | \
	sed -r 's/^#define +SNDRV_PCM_IOCTL_([A-Z0-9_]+)[\t ]+_IO[RW]*\( *.A., *(0x[[:xdigit:]]+),?.*/\t[\2] = \"\1\",/g'
printf "};\n"
