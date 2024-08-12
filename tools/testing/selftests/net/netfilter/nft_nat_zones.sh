#!/bin/bash
#
# Test connection tracking zone and NAT source port reallocation support.
#

source lib.sh

# Don't increase too much, 2000 clients should work
# just fine but script can then take several minutes with
# KASAN/debug builds.
maxclients=100

have_socat=0
ret=0

[ "$KSFT_MACHINE_SLOW" = yes ] && maxclients=40
# client1---.
#            veth1-.
#                  |
#               NAT Gateway --veth0--> Server
#                  | |
#            veth2-' |
# client2---'        |
#  ....              |
# clientX----vethX---'

# All clients share identical IP address.
# NAT Gateway uses policy routing and conntrack zones to isolate client
# namespaces.  Each client connects to Server, each with colliding tuples:
#   clientsaddr:10000 -> serveraddr:dport
#   NAT Gateway is supposed to do port reallocation for each of the
#   connections.

v4gc1=$(sysctl -n net.ipv4.neigh.default.gc_thresh1 2>/dev/null)
v4gc2=$(sysctl -n net.ipv4.neigh.default.gc_thresh2 2>/dev/null)
v4gc3=$(sysctl -n net.ipv4.neigh.default.gc_thresh3 2>/dev/null)
v6gc1=$(sysctl -n net.ipv6.neigh.default.gc_thresh1 2>/dev/null)
v6gc2=$(sysctl -n net.ipv6.neigh.default.gc_thresh2 2>/dev/null)
v6gc3=$(sysctl -n net.ipv6.neigh.default.gc_thresh3 2>/dev/null)

cleanup()
{
	cleanup_all_ns

	sysctl -q net.ipv4.neigh.default.gc_thresh1="$v4gc1" 2>/dev/null
	sysctl -q net.ipv4.neigh.default.gc_thresh2="$v4gc2" 2>/dev/null
	sysctl -q net.ipv4.neigh.default.gc_thresh3="$v4gc3" 2>/dev/null
	sysctl -q net.ipv6.neigh.default.gc_thresh1="$v6gc1" 2>/dev/null
	sysctl -q net.ipv6.neigh.default.gc_thresh2="$v6gc2" 2>/dev/null
	sysctl -q net.ipv6.neigh.default.gc_thresh3="$v6gc3" 2>/dev/null
}

checktool "nft --version" echo "run test without nft tool"
checktool "conntrack -V" "run test without conntrack tool"

if socat -h >/dev/null 2>&1; then
	have_socat=1
fi

setup_ns gw srv

trap cleanup EXIT

ip link add veth0 netns "$gw" type veth peer name eth0 netns "$srv"
ip -net "$gw" link set veth0 up
ip -net "$srv" link set eth0 up

sysctl -q net.ipv6.neigh.default.gc_thresh1=512  2>/dev/null
sysctl -q net.ipv6.neigh.default.gc_thresh2=1024 2>/dev/null
sysctl -q net.ipv6.neigh.default.gc_thresh3=4096 2>/dev/null
sysctl -q net.ipv4.neigh.default.gc_thresh1=512  2>/dev/null
sysctl -q net.ipv4.neigh.default.gc_thresh2=1024 2>/dev/null
sysctl -q net.ipv4.neigh.default.gc_thresh3=4096 2>/dev/null

for i in $(seq 1 "$maxclients");do
  setup_ns "cl$i"

  cl=$(eval echo \$cl"$i")
  if ! ip link add veth"$i" netns "$gw" type veth peer name eth0 netns "$cl" > /dev/null 2>&1;then
    echo "SKIP: No virtual ethernet pair device support in kernel"
    exit $ksft_skip
  fi
done

for i in $(seq 1 "$maxclients");do
  cl=$(eval echo \$cl"$i")
  echo netns exec "$cl" ip link set eth0 up
  echo netns exec "$cl" sysctl -q net.ipv4.tcp_syn_retries=2
  echo netns exec "$gw" ip link set "veth$i" up
  echo netns exec "$gw" sysctl -q net.ipv4.conf.veth"$i".arp_ignore=2
  echo netns exec "$gw" sysctl -q net.ipv4.conf.veth"$i".rp_filter=0

  # clients have same IP addresses.
  echo netns exec "$cl" ip addr add 10.1.0.3/24 dev eth0
  echo netns exec "$cl" ip addr add dead:1::3/64 dev eth0 nodad
  echo netns exec "$cl" ip route add default via 10.1.0.2 dev eth0
  echo netns exec "$cl" ip route add default via dead:1::2 dev eth0

  # NB: same addresses on client-facing interfaces.
  echo netns exec "$gw" ip addr add 10.1.0.2/24 dev "veth$i"
  echo netns exec "$gw" ip addr add dead:1::2/64 dev "veth$i" nodad

  # gw: policy routing
  echo netns exec "$gw" ip route add 10.1.0.0/24 dev "veth$i" table $((1000+i))
  echo netns exec "$gw" ip route add dead:1::0/64 dev "veth$i" table $((1000+i))
  echo netns exec "$gw" ip route add 10.3.0.0/24 dev veth0 table $((1000+i))
  echo netns exec "$gw" ip route add dead:3::0/64 dev veth0 table $((1000+i))
  echo netns exec "$gw" ip rule add fwmark "$i" lookup $((1000+i))
done | ip -batch /dev/stdin

ip -net "$gw" addr add 10.3.0.1/24 dev veth0
ip -net "$gw" addr add dead:3::1/64 dev veth0 nodad

ip -net "$srv" addr add 10.3.0.99/24 dev eth0
ip -net "$srv" addr add dead:3::99/64 dev eth0 nodad

ip netns exec "$gw" nft -f /dev/stdin<<EOF
table inet raw {
	map iiftomark {
		type ifname : mark
	}

	map iiftozone {
		typeof iifname : ct zone
	}

	set inicmp {
		flags dynamic
		type ipv4_addr . ifname . ipv4_addr
	}
	set inflows {
		flags dynamic
		type ipv4_addr . inet_service . ifname . ipv4_addr . inet_service
	}

	set inflows6 {
		flags dynamic
		type ipv6_addr . inet_service . ifname . ipv6_addr . inet_service
	}

	chain prerouting {
		type filter hook prerouting priority -64000; policy accept;
		ct original zone set meta iifname map @iiftozone
		meta mark set meta iifname map @iiftomark

		tcp flags & (syn|ack) == ack add @inflows { ip saddr . tcp sport . meta iifname . ip daddr . tcp dport counter }
		add @inflows6 { ip6 saddr . tcp sport . meta iifname . ip6 daddr . tcp dport counter }
		ip protocol icmp add @inicmp { ip saddr . meta iifname . ip daddr counter }
	}

	chain nat_postrouting {
		type nat hook postrouting priority 0; policy accept;
                ct mark set meta mark meta oifname veth0 masquerade
	}

	chain mangle_prerouting {
		type filter hook prerouting priority -100; policy accept;
		ct direction reply meta mark set ct mark
	}
}
EOF
if [ "$?" -ne 0 ];then
	echo "SKIP: Could not add nftables rules"
	exit $ksft_skip
fi

( echo add element inet raw iiftomark \{
	for i in $(seq 1 $((maxclients-1))); do
		echo \"veth"$i"\" : "$i",
	done
	echo \"veth"$maxclients"\" : "$maxclients" \}
	echo add element inet raw iiftozone \{
	for i in $(seq 1 $((maxclients-1))); do
		echo \"veth"$i"\" : "$i",
	done
	echo \"veth$maxclients\" : $maxclients \}
) | ip netns exec "$gw" nft -f /dev/stdin

ip netns exec "$gw" sysctl -q net.ipv4.conf.all.forwarding=1 > /dev/null
ip netns exec "$gw" sysctl -q net.ipv6.conf.all.forwarding=1 > /dev/null
ip netns exec "$gw" sysctl -q net.ipv4.conf.all.rp_filter=0 >/dev/null

# useful for debugging: allows to use 'ping' from clients to gateway.
ip netns exec "$gw" sysctl -q net.ipv4.fwmark_reflect=1 > /dev/null
ip netns exec "$gw" sysctl -q net.ipv6.fwmark_reflect=1 > /dev/null

for i in $(seq 1 "$maxclients"); do
  cl=$(eval echo \$cl"$i")
  ip netns exec "$cl" ping -i 0.5 -q -c 3 10.3.0.99 > /dev/null 2>&1 &
done

wait || ret=1

[ "$ret" -ne 0 ] && "FAIL: Ping failure from $cl" 1>&2

for i in $(seq 1 "$maxclients"); do
   if ! ip netns exec "$gw" nft get element inet raw inicmp "{ 10.1.0.3 . \"veth$i\" . 10.3.0.99 }" | grep -q "{ 10.1.0.3 . \"veth$i\" . 10.3.0.99 counter packets 3 bytes 252 }"; then
      ret=1
      echo "FAIL: counter icmp mismatch for veth$i" 1>&2
      ip netns exec "$gw" nft get element inet raw inicmp "{ 10.1.0.3 . \"veth$i\" . 10.3.0.99 }" 1>&2
      break
   fi
done

if ! ip netns exec "$gw" nft get element inet raw inicmp "{ 10.3.0.99 . \"veth0\" . 10.3.0.1 }" | grep -q "{ 10.3.0.99 . \"veth0\" . 10.3.0.1 counter packets $((3 * maxclients)) bytes $((252 * maxclients)) }"; then
    ret=1
    echo "FAIL: counter icmp mismatch for veth0: { 10.3.0.99 . \"veth0\" . 10.3.0.1 counter packets $((3 * maxclients)) bytes $((252 * maxclients)) }"
    ip netns exec "$gw" nft get element inet raw inicmp "{ 10.3.99 . \"veth0\" . 10.3.0.1 }" 1>&2
fi

if [ $ret -eq 0 ]; then
	echo "PASS: ping test from all $maxclients namespaces"
fi

if [ $have_socat -eq 0 ];then
	echo "SKIP: socat not installed"
	if [ $ret -ne 0 ];then
	    exit $ret
	fi
	exit $ksft_skip
fi

listener_ready()
{
	ss -N "$1" -lnt -o "sport = :5201" | grep -q 5201
}

ip netns exec "$srv" socat -u TCP-LISTEN:5201,fork STDOUT > /dev/null 2>/dev/null &
socatpid=$!

busywait 1000 listener_ready "$srv"

for i in $(seq 1 "$maxclients"); do
  if [ $ret -ne 0 ]; then
     break
  fi
  cl=$(eval echo \$cl"$i")
  if ! ip netns exec "$cl" socat -4 -u STDIN TCP:10.3.0.99:5201,sourceport=10000 < /dev/null > /dev/null; then
     echo "FAIL: Failure to connect for $cl" 1>&2
     ip netns exec "$gw" conntrack -S 1>&2
     ret=1
  fi
done
if [ $ret -eq 0 ];then
	echo "PASS: socat connections for all $maxclients net namespaces"
fi

kill $socatpid
wait

for i in $(seq 1 "$maxclients"); do
   if ! ip netns exec "$gw" nft get element inet raw inflows "{ 10.1.0.3 . 10000 . \"veth$i\" . 10.3.0.99 . 5201 }" > /dev/null;then
      ret=1
      echo "FAIL: can't find expected tcp entry for veth$i" 1>&2
      break
   fi
done
if [ $ret -eq 0 ];then
	echo "PASS: Found client connection for all $maxclients net namespaces"
fi

if ! ip netns exec "$gw" nft get element inet raw inflows "{ 10.3.0.99 . 5201 . \"veth0\" . 10.3.0.1 . 10000 }" > /dev/null;then
    ret=1
    echo "FAIL: cannot find return entry on veth0" 1>&2
fi

exit $ret
