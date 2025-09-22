#!/bin/ksh
#	$Id: vxlan_2.sh,v 1.5 2024/02/29 06:54:29 anton Exp $


CAPFILE=$(mktemp -t regress_vxlan.XXXXXXX)


CURDIR=$(cd $(dirname $0); pwd)


cleanup() {
	for ifname in $CLEANUP_IFS; do
		$SUDO ifconfig $ifname down
	done
	for ifname in $CLEANUP_IFS; do
		$SUDO ifconfig $ifname destroy
	done
	rm $CAPFILE
}

do_ping()
{
	local source="$1"
	local dest="${VXLAN_NETID}${2}"
	$PING -q -c 1 -w 1 -V "$source" "$dest" > /dev/null # warm up arp
	$PING -q -c 3 -w 1 -V "$source" "$dest" | grep -q ' 0.0% packet loss' && return
	echo "Failed to ping $dest from vstack $source"
	STATUS=1
}

cross_ping() {
	local tcpdump_expr='udp src port 4789 and dst port 4789 and (ether multicast or ip multicast)'
	local nomcast=
	[[ $1 == nomcast ]] && nomcast=1 && shift
	echo "cross_ping: vstacks=$@, ping=$PING, nomcast=$nomcast"
	while [[ $# -gt 1 ]]; do
		local source=$1 ; shift
		:> $CAPFILE
		if [[ -n $nomcast ]]; then
			$SUDO tcpdump -i pair${source}0 -n -s 512 -w $CAPFILE "$tcpdump_expr" 2> /dev/null &
			while ! [[ -s $CAPFILE ]]; do :; done
		fi
		for target in $@ ; do
			do_ping $source $target
		done
		sleep 1
		if [[ -n $nomcast ]]; then
			$SUDO pkill -f "tcpdump -i pair${source}0 -n"
			wait
			if $SUDO tcpdump -s 512 -nr $CAPFILE | grep '.'; then
				echo "Multicast traffic detected when pinging from rdomain $1"
				STATUS=1
			fi
		fi
	done
}

test_inet6()
{
	VXLAN_NETID="fd42::"
	PING=ping6
	for vstack in "$@"; do
		$SUDO ifconfig "vxlan$vstack" inet6 "${VXLAN_NETID}${vstack}/64"
	done
	sleep 2 # sleep off DAD
	cross_ping "$@"
	[[ -n $DYNAMIC ]] && cross_ping ${DYNAMIC:+nomcast} "$@"
	for vstack in "$@"; do
		$SUDO ifconfig "vxlan$vstack" inet6 "${VXLAN_NETID}${vstack}/64" delete
	done
}

test_inet()
{
	VXLAN_NETID="10.42.0."
	PING=ping
	for vstack in "$@"; do
		$SUDO ifconfig "vxlan$vstack" inet "${VXLAN_NETID}${vstack}/24"
	done
	cross_ping "$@"
	[[ -n $DYNAMIC ]] && cross_ping ${DYNAMIC:+nomcast} "$@"
	for vstack in "$@"; do
		$SUDO ifconfig "vxlan$vstack" inet "${VXLAN_NETID}${vstack}/24" delete
	done
}

vstack_add() {
	local vstack=$1
	local vstack_pairname="pair${vstack}0"
	local vstack_tunsrc="${PAIR_NETID}${vstack}"
	[[ $AF == inet6 ]] && tundst_sufx="%${vstack_pairname}"

	for ifname in "$vstack_pairname" "vxlan$vstack" "bridge$vstack"; do
		iface_exists $ifname && abort_test "interface $ifname already exists"
		CLEANUP_IFS="$ifname $CLEANUP_IFS"
	done
	$SUDO ifconfig "$vstack_pairname" rdomain "$vstack" $IFCONFIG_OPTS
	$SUDO ifconfig "$vstack_pairname" "$AF" "${vstack_tunsrc}${PAIR_PREFX}" up
	$SUDO ifconfig "vxlan$vstack" rdomain "$vstack" tunneldomain "$vstack" $IFCONFIG_OPTS
	$SUDO ifconfig "vxlan$vstack" vnetid "$VNETID" tunnel "$vstack_tunsrc" "${VXLAN_TUNDST}${tundst_sufx}" parent "$vstack_pairname" up
	[[ -n $DYNAMIC ]] && $SUDO ifconfig "bridge$vstack" rdomain "$vstack" add "vxlan$vstack" $IFCONFIG_OPTS up
}


. ${CURDIR}/vxlan_subr

# Use the first rdomain as the vnetid.
VNETID="$(set -- $RDOMAINS; echo $1)"
RDOMAINS="$(set -- $RDOMAINS; shift 1; echo $@)"

for rdom in $RDOMAINS; do
	rdomain_is_used $rdom || abort_test "rdomain $rdom already in use"
done

rdomain_is_used $VNETID || abort_test "rdomain $rdom already in use"
iface_exists "bridge$VNETID" && abort_test "interface bridge${VNETID} already exists"
$SUDO ifconfig "bridge$VNETID" rdomain "$VNETID" $IFCONFIG_OPTS up

case $AF in
	inet)
		PAIR_NETID="10.23.0."
		PAIR_PREFX="/24"
		VXLAN_TUNDST="224.0.2.$VNETID"
		;;
	inet6)
		PAIR_NETID="fd23::"
		PAIR_PREFX="/64"
		VXLAN_TUNDST="ff02::$VNETID"
		;;
	*)
		echo "Unknown AF $AF !"
		exit 1
esac

trap cleanup 0 1 2 3 15

for id in $RDOMAINS; do
	vstack_add $id
	iface_exists "pair${id}1" && abort_test "interface pair${id}1 already exists"
	$SUDO ifconfig "pair${id}1" rdomain $VNETID $IFCONFIG_OPTS up
	CLEANUP_IFS="pair${id}1 $CLEANUP_IFS"
	$SUDO ifconfig "pair${id}1" patch "pair${id}0"
	$SUDO ifconfig "bridge$VNETID" add "pair${id}1"
done

CLEANUP_IFS="bridge$VNETID $CLEANUP_IFS"

for id in $RDOMAINS; do
	CLEANUP_IFS="$CLEANUP_IFS lo${id}"
done
CLEANUP_IFS="$CLEANUP_IFS lo${VNETID}"

STATUS=0

test_inet $RDOMAINS

test_inet6 $RDOMAINS

exit $STATUS
