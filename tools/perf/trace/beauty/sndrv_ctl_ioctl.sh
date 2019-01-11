#!/bin/sh

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/sound/

printf "static const char *sndrv_ctl_ioctl_cmds[] = {\n"
grep "^#define[\t ]\+SNDRV_CTL_IOCTL_" $header_dir/asound.h | \
	sed -r 's/^#define +SNDRV_CTL_IOCTL_([A-Z0-9_]+)[\t ]+_IO[RW]*\( *.U., *(0x[[:xdigit:]]+),?.*/\t[\2] = \"\1\",/g'
printf "};\n"
