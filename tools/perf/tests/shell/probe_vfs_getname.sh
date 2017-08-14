# Add vfs_getname probe to get syscall args filenames
#
# Arnaldo Carvalho de Melo <acme@kernel.org>, 2017

. $(dirname $0)/lib/probe_vfs_getname.sh

add_probe_vfs_getname || skip_if_no_debuginfo
err=$?
cleanup_probe_vfs_getname
exit $err
