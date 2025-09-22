
# $OpenBSD: nonxt_reflect.sh,v 1.1 2018/05/21 01:19:21 bluhm Exp $

if [ -z "$local_addresses" ]; then
	echo no local addresses configured >&2
	exit 1
fi

daemon=/usr/src/regress/sys/netinet/ipsec/obj/nonxt-reflect
if ! [ -x $daemon ]; then
	daemon=/usr/src/regress/sys/netinet/ipsec/nonxt-reflect
fi
if ! [ -x $daemon ]; then
	echo executable $daemon not found >&2
	exit 1
fi

. /etc/rc.d/rc.subr

rc_reload=NO

pexp="${daemon}${daemon_flags:+ ${daemon_flags}} [0-9a-f.:][0-9a-f.:]*"

rc_start() {
	for ip in $local_addresses; do
		${rcexec} "${daemon} ${daemon_flags} $ip"
	done
}

rc_cmd $1
