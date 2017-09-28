#!/bin/sh

vhost_virtio_header_dir=$1

printf "static const char *vhost_virtio_ioctl_cmds[] = {\n"
regex='^#[[:space:]]*define[[:space:]]+VHOST_(\w+)[[:space:]]+_IOW?\([[:space:]]*VHOST_VIRTIO[[:space:]]*,[[:space:]]*(0x[[:xdigit:]]+).*'
egrep $regex ${vhost_virtio_header_dir}/vhost.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

printf "static const char *vhost_virtio_ioctl_read_cmds[] = {\n"
regex='^#[[:space:]]*define[[:space:]]+VHOST_(\w+)[[:space:]]+_IOW?R\([[:space:]]*VHOST_VIRTIO[[:space:]]*,[[:space:]]*(0x[[:xdigit:]]+).*'
egrep $regex ${vhost_virtio_header_dir}/vhost.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"
