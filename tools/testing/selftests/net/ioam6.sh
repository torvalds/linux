#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Author: Justin Iurman <justin.iurman@uliege.be>
#
# This script evaluates IOAM for IPv6 by checking local IOAM configurations and
# IOAM data inside packets. There are three categories of tests: LOCAL, OUTPUT,
# and INPUT. The former (LOCAL) checks all IOAM related configurations locally
# without sending packets. OUTPUT tests verify the processing of an IOAM
# encapsulating node, while INPUT tests verify the processing of an IOAM transit
# node. Both OUTPUT and INPUT tests send packets. Each test is documented inside
# its own handler.
#
# The topology used for OUTPUT and INPUT tests is made of three nodes:
# - Alpha (the IOAM encapsulating node)
# - Beta  (the IOAM transit node)
# - Gamma (the receiver) **
#
# An IOAM domain is configured from Alpha to Beta, but not on the reverse path.
# Alpha adds an IOAM option (Pre-allocated Trace) inside a Hop-by-hop.
#
# ** Gamma is required because ioam6_parser.c uses a packet socket and we need
#    to see IOAM data inserted by the very last node (Beta), which would happen
#    _after_ we get a copy of the packet on Beta. Note that using an
#    IPv6 raw socket with IPV6_RECVHOPOPTS on Beta would not be enough: we also
#    need to access the IPv6 header to check some fields (e.g., source and
#    destination addresses), which is not possible in that case. As a
#    consequence, we need Gamma as a receiver to run ioam6_parser.c which uses a
#    packet socket.
#
#
#         +-----------------------+          +-----------------------+
#         |                       |          |                       |
#         |      Alpha netns      |          |      Gamma netns      |
#         |                       |          |                       |
#         | +-------------------+ |          | +-------------------+ |
#         | |       veth0       | |          | |       veth0       | |
#         | | 2001:db8:1::2/64  | |          | | 2001:db8:2::2/64  | |
#         | +-------------------+ |          | +-------------------+ |
#         |           .           |          |           .           |
#         +-----------.-----------+          +-----------.-----------+
#                     .                                  .
#                     .                                  .
#                     .                                  .
#         +-----------.----------------------------------.-----------+
#         |           .                                  .           |
#         | +-------------------+              +-------------------+ |
#         | |       veth0       |              |       veth1       | |
#         | | 2001:db8:1::1/64  | ............ | 2001:db8:2::1/64  | |
#         | +-------------------+              +-------------------+ |
#         |                                                          |
#         |                        Beta netns                        |
#         |                                                          |
#         +----------------------------------------------------------+
#
#
#
#         +==========================================================+
#         |                Alpha - IOAM configuration                |
#         +=====================+====================================+
#         | Node ID             | 1                                  |
#         +---------------------+------------------------------------+
#         | Node Wide ID        | 11111111                           |
#         +---------------------+------------------------------------+
#         | Ingress ID          | 0xffff (default value)             |
#         +---------------------+------------------------------------+
#         | Ingress Wide ID     | 0xffffffff (default value)         |
#         +---------------------+------------------------------------+
#         | Egress ID           | 101                                |
#         +---------------------+------------------------------------+
#         | Egress Wide ID      | 101101                             |
#         +---------------------+------------------------------------+
#         | Namespace Data      | 0xdeadbeef                         |
#         +---------------------+------------------------------------+
#         | Namespace Wide Data | 0xcafec0caf00dc0de                 |
#         +---------------------+------------------------------------+
#         | Schema ID           | 777                                |
#         +---------------------+------------------------------------+
#         | Schema Data         | something that will be 4n-aligned  |
#         +---------------------+------------------------------------+
#
#
#         +==========================================================+
#         |                 Beta - IOAM configuration                |
#         +=====================+====================================+
#         | Node ID             | 2                                  |
#         +---------------------+------------------------------------+
#         | Node Wide ID        | 22222222                           |
#         +---------------------+------------------------------------+
#         | Ingress ID          | 201                                |
#         +---------------------+------------------------------------+
#         | Ingress Wide ID     | 201201                             |
#         +---------------------+------------------------------------+
#         | Egress ID           | 202                                |
#         +---------------------+------------------------------------+
#         | Egress Wide ID      | 202202                             |
#         +---------------------+------------------------------------+
#         | Namespace Data      | 0xffffffff (default value)         |
#         +---------------------+------------------------------------+
#         | Namespace Wide Data | 0xffffffffffffffff (default value) |
#         +---------------------+------------------------------------+
#         | Schema ID           | 0xffffff (= None)                  |
#         +---------------------+------------------------------------+
#         | Schema Data         |                                    |
#         +---------------------+------------------------------------+

source lib.sh

################################################################################
#                                                                              #
# WARNING: Be careful if you modify the block below - it MUST be kept          #
#          synchronized with configurations inside ioam6_parser.c and always   #
#          reflect the same.                                                   #
#                                                                              #
################################################################################

ALPHA=(
  1                                    # ID
  11111111                             # Wide ID
  0xffff                               # Ingress ID (default value)
  0xffffffff                           # Ingress Wide ID (default value)
  101                                  # Egress ID
  101101                               # Egress Wide ID
  0xdeadbeef                           # Namespace Data
  0xcafec0caf00dc0de                   # Namespace Wide Data
  777                                  # Schema ID
  "something that will be 4n-aligned"  # Schema Data
)

BETA=(
  2                                    # ID
  22222222                             # Wide ID
  201                                  # Ingress ID
  201201                               # Ingress Wide ID
  202                                  # Egress ID
  202202                               # Egress Wide ID
  0xffffffff                           # Namespace Data (empty value)
  0xffffffffffffffff                   # Namespace Wide Data (empty value)
  0xffffff                             # Schema ID (empty value)
  ""                                   # Schema Data (empty value)
)

TESTS_LOCAL="
  local_sysctl_ioam_id
  local_sysctl_ioam_id_wide
  local_sysctl_ioam_intf_id
  local_sysctl_ioam_intf_id_wide
  local_sysctl_ioam_intf_enabled
  local_ioam_namespace
  local_ioam_schema
  local_ioam_schema_namespace
  local_route_ns
  local_route_tunsrc
  local_route_tundst
  local_route_trace_type
  local_route_trace_size
  local_route_trace_type_bits
  local_route_trace_size_values
"

TESTS_OUTPUT="
  output_undef_ns
  output_no_room
  output_no_room_oss
  output_bits
  output_sizes
  output_full_supp_trace
"

TESTS_INPUT="
  input_undef_ns
  input_no_room
  input_no_room_oss
  input_disabled
  input_oflag
  input_bits
  input_sizes
  input_full_supp_trace
"

################################################################################
#                                                                              #
#                                   LIBRARY                                    #
#                                                                              #
################################################################################

check_kernel_compatibility()
{
  setup_ns ioam_tmp_node &>/dev/null
  local ret=$?

  ip link add name veth0 netns $ioam_tmp_node type veth \
    peer name veth1 netns $ioam_tmp_node &>/dev/null
  ret=$((ret + $?))

  ip -netns $ioam_tmp_node link set veth0 up &>/dev/null
  ret=$((ret + $?))

  ip -netns $ioam_tmp_node link set veth1 up &>/dev/null
  ret=$((ret + $?))

  if [ $ret != 0 ]
  then
    echo "SKIP: Setup failed."
    cleanup_ns $ioam_tmp_node
    exit $ksft_skip
  fi

  ip -netns $ioam_tmp_node route add 2001:db8:2::/64 \
    encap ioam6 trace prealloc type 0x800000 ns 0 size 4 dev veth0 &>/dev/null
  ret=$?

  ip -netns $ioam_tmp_node -6 route 2>/dev/null | grep -q "encap ioam6"
  ret=$((ret + $?))

  if [ $ret != 0 ]
  then
    echo "SKIP: Cannot attach an IOAM trace to a route. Was your kernel" \
         "compiled without CONFIG_IPV6_IOAM6_LWTUNNEL? Are you running an" \
         "old kernel? Are you using an old version of iproute2?"
    cleanup_ns $ioam_tmp_node
    exit $ksft_skip
  fi

  cleanup_ns $ioam_tmp_node

  lsmod 2>/dev/null | grep -q "ip6_tunnel"
  ip6tnl_loaded=$?

  if [ $ip6tnl_loaded == 0 ]
  then
    encap_tests=0
  else
    modprobe ip6_tunnel &>/dev/null
    lsmod 2>/dev/null | grep -q "ip6_tunnel"
    encap_tests=$?

    if [ $encap_tests != 0 ]
    then
      ip a 2>/dev/null | grep -q "ip6tnl0"
      encap_tests=$?

      if [ $encap_tests != 0 ]
      then
        echo "Note: ip6_tunnel not found neither as a module nor inside the" \
             "kernel. Any tests that require it will be skipped."
      fi
    fi
  fi
}

cleanup()
{
  cleanup_ns $ioam_node_alpha $ioam_node_beta $ioam_node_gamma

  if [ $ip6tnl_loaded != 0 ]
  then
    modprobe -r ip6_tunnel &>/dev/null
  fi
}

setup()
{
  setup_ns ioam_node_alpha ioam_node_beta ioam_node_gamma &>/dev/null

  ip link add name ioam-veth-alpha netns $ioam_node_alpha type veth \
    peer name ioam-veth-betaL netns $ioam_node_beta &>/dev/null
  ip link add name ioam-veth-betaR netns $ioam_node_beta type veth \
    peer name ioam-veth-gamma netns $ioam_node_gamma &>/dev/null

  ip -netns $ioam_node_alpha link set ioam-veth-alpha name veth0 &>/dev/null
  ip -netns $ioam_node_beta link set ioam-veth-betaL name veth0 &>/dev/null
  ip -netns $ioam_node_beta link set ioam-veth-betaR name veth1 &>/dev/null
  ip -netns $ioam_node_gamma link set ioam-veth-gamma name veth0 &>/dev/null

  ip -netns $ioam_node_alpha addr add 2001:db8:1::50/64 dev veth0 &>/dev/null
  ip -netns $ioam_node_alpha addr add 2001:db8:1::2/64 dev veth0 &>/dev/null
  ip -netns $ioam_node_alpha link set veth0 up &>/dev/null
  ip -netns $ioam_node_alpha link set lo up &>/dev/null
  ip -netns $ioam_node_alpha route add 2001:db8:2::/64 \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  ip -netns $ioam_node_beta addr add 2001:db8:1::1/64 dev veth0 &>/dev/null
  ip -netns $ioam_node_beta addr add 2001:db8:2::1/64 dev veth1 &>/dev/null
  ip -netns $ioam_node_beta link set veth0 up &>/dev/null
  ip -netns $ioam_node_beta link set veth1 up &>/dev/null
  ip -netns $ioam_node_beta link set lo up &>/dev/null

  ip -netns $ioam_node_gamma addr add 2001:db8:2::2/64 dev veth0 &>/dev/null
  ip -netns $ioam_node_gamma link set veth0 up &>/dev/null
  ip -netns $ioam_node_gamma link set lo up &>/dev/null
  ip -netns $ioam_node_gamma route add 2001:db8:1::/64 \
    via 2001:db8:2::1 dev veth0 &>/dev/null

  # - Alpha: IOAM config -
  ip netns exec $ioam_node_alpha \
    sysctl -wq net.ipv6.ioam6_id=${ALPHA[0]} &>/dev/null
  ip netns exec $ioam_node_alpha \
    sysctl -wq net.ipv6.ioam6_id_wide=${ALPHA[1]} &>/dev/null
  ip netns exec $ioam_node_alpha \
    sysctl -wq net.ipv6.conf.veth0.ioam6_id=${ALPHA[4]} &>/dev/null
  ip netns exec $ioam_node_alpha \
    sysctl -wq net.ipv6.conf.veth0.ioam6_id_wide=${ALPHA[5]} &>/dev/null
  ip -netns $ioam_node_alpha \
    ioam namespace add 123 data ${ALPHA[6]} wide ${ALPHA[7]} &>/dev/null
  ip -netns $ioam_node_alpha \
    ioam schema add ${ALPHA[8]} "${ALPHA[9]}" &>/dev/null
  ip -netns $ioam_node_alpha \
    ioam namespace set 123 schema ${ALPHA[8]} &>/dev/null

  # - Beta: IOAM config -
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.all.forwarding=1 &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.ioam6_id=${BETA[0]} &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.ioam6_id_wide=${BETA[1]} &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=1 &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_id=${BETA[2]} &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_id_wide=${BETA[3]} &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth1.ioam6_id=${BETA[4]} &>/dev/null
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth1.ioam6_id_wide=${BETA[5]} &>/dev/null
  ip -netns $ioam_node_beta ioam namespace add 123 &>/dev/null

  sleep 1

  ip netns exec $ioam_node_alpha ping6 -c 5 -W 1 2001:db8:2::2 &>/dev/null
  if [ $? != 0 ]
  then
    echo "SKIP: Setup failed."
    cleanup
    exit $ksft_skip
  fi
}

log_test_passed()
{
  printf " - TEST: %-57s  [ OK ]\n" "$1"
  npassed=$((npassed+1))
}

log_test_skipped()
{
  printf " - TEST: %-57s  [SKIP]\n" "$1"
  nskipped=$((nskipped+1))
}

log_test_failed()
{
  printf " - TEST: %-57s  [FAIL]\n" "$1"
  nfailed=$((nfailed+1))
}

run_test()
{
  local name=$1
  local desc=$2
  local ip6_src=$3
  local trace_type=$4
  local trace_size=$5
  local ioam_ns=$6
  local type=$7

  ip netns exec $ioam_node_gamma \
    ./ioam6_parser veth0 $name $ip6_src 2001:db8:2::2 \
                   $trace_type $trace_size $ioam_ns $type &
  local spid=$!
  sleep 0.1

  ip netns exec $ioam_node_alpha ping6 -t 64 -c 1 -W 1 2001:db8:2::2 &>/dev/null
  if [ $? != 0 ]
  then
    log_test_failed "${desc}"
    kill -2 $spid &>/dev/null
  else
    wait $spid
    [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
  fi
}

run()
{
  local test

  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo
  printf "| %-28s LOCAL tests %-29s |"
  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo

  echo
  echo "Global config"
  for test in $TESTS_LOCAL
  do
    $test
  done

  echo
  echo "Inline mode"
  for test in $TESTS_LOCAL
  do
    $test "inline"
  done

  echo
  echo "Encap mode"
  for test in $TESTS_LOCAL
  do
    $test "encap"
  done

  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo
  printf "| %-28s OUTPUT tests %-28s |"
  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo

  # set OUTPUT settings
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=0 &>/dev/null

  echo
  echo "Inline mode"
  for test in $TESTS_OUTPUT
  do
    $test "inline"
  done

  echo
  echo "Encap mode"
  for test in $TESTS_OUTPUT
  do
    $test "encap"
  done

  echo
  echo "Encap mode (with tunsrc)"
  for test in $TESTS_OUTPUT
  do
    $test "encap" "tunsrc"
  done

  # clean OUTPUT settings
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=1 &>/dev/null

  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo
  printf "| %-28s INPUT tests %-29s |"
  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo

  # set INPUT settings
  ip -netns $ioam_node_alpha ioam namespace del 123 &>/dev/null

  echo
  echo "Inline mode"
  for test in $TESTS_INPUT
  do
    $test "inline"
  done

  echo
  echo "Encap mode"
  for test in $TESTS_INPUT
  do
    $test "encap"
  done

  # clean INPUT settings
  ip -netns $ioam_node_alpha \
    ioam namespace add 123 data ${ALPHA[6]} wide ${ALPHA[7]} &>/dev/null
  ip -netns $ioam_node_alpha \
    ioam namespace set 123 schema ${ALPHA[8]} &>/dev/null

  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo
  printf "| %-30s Results %-31s |"
  echo
  printf "+"
  printf "%0.s-" {1..72}
  printf "+"
  echo

  echo
  echo "- Passed:  ${npassed}"
  echo "- Skipped: ${nskipped}"
  echo "- Failed:  ${nfailed}"
  echo
}

bit2type=(
  0x800000 0x400000 0x200000 0x100000 0x080000 0x040000 0x020000 0x010000
  0x008000 0x004000 0x002000 0x001000 0x000800 0x000400 0x000200 0x000100
  0x000080 0x000040 0x000020 0x000010 0x000008 0x000004 0x000002 0x000001
)
bit2size=( 4 4 4 4 4 4 4 4 8 8 8 4 4 4 4 4 4 4 4 4 4 4 4 0 )


################################################################################
#                                                                              #
#                                 LOCAL tests                                  #
#                                                                              #
################################################################################

local_sysctl_ioam_id()
{
  ##############################################################################
  # Make sure the sysctl "net.ipv6.ioam6_id" works as expected.                #
  ##############################################################################
  local desc="Sysctl net.ipv6.ioam6_id"

  [ ! -z $1 ] && return

  ip netns exec $ioam_node_alpha \
    sysctl net.ipv6.ioam6_id 2>/dev/null | grep -wq ${ALPHA[0]}

  [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_sysctl_ioam_id_wide()
{
  ##############################################################################
  # Make sure the sysctl "net.ipv6.ioam6_id_wide" works as expected.           #
  ##############################################################################
  local desc="Sysctl net.ipv6.ioam6_id_wide"

  [ ! -z $1 ] && return

  ip netns exec $ioam_node_alpha \
    sysctl net.ipv6.ioam6_id_wide 2>/dev/null | grep -wq ${ALPHA[1]}

  [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_sysctl_ioam_intf_id()
{
  ##############################################################################
  # Make sure the sysctl "net.ipv6.conf.XX.ioam6_id" works as expected.        #
  ##############################################################################
  local desc="Sysctl net.ipv6.conf.XX.ioam6_id"

  [ ! -z $1 ] && return

  ip netns exec $ioam_node_alpha \
    sysctl net.ipv6.conf.veth0.ioam6_id 2>/dev/null | grep -wq ${ALPHA[4]}

  [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_sysctl_ioam_intf_id_wide()
{
  ##############################################################################
  # Make sure the sysctl "net.ipv6.conf.XX.ioam6_id_wide" works as expected.   #
  ##############################################################################
  local desc="Sysctl net.ipv6.conf.XX.ioam6_id_wide"

  [ ! -z $1 ] && return

  ip netns exec $ioam_node_alpha \
    sysctl net.ipv6.conf.veth0.ioam6_id_wide 2>/dev/null | grep -wq ${ALPHA[5]}

  [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_sysctl_ioam_intf_enabled()
{
  ##############################################################################
  # Make sure the sysctl "net.ipv6.conf.XX.ioam6_enabled" works as expected.   #
  ##############################################################################
  local desc="Sysctl net.ipv6.conf.XX.ioam6_enabled"

  [ ! -z $1 ] && return

  ip netns exec $ioam_node_beta \
    sysctl net.ipv6.conf.veth0.ioam6_enabled 2>/dev/null | grep -wq 1

  [ $? == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_ioam_namespace()
{
  ##############################################################################
  # Make sure the creation of an IOAM Namespace works as expected.             #
  ##############################################################################
  local desc="Create an IOAM Namespace"

  [ ! -z $1 ] && return

  ip -netns $ioam_node_alpha \
    ioam namespace show 2>/dev/null | grep -wq 123
  local ret=$?

  ip -netns $ioam_node_alpha \
    ioam namespace show 2>/dev/null | grep -wq ${ALPHA[6]}
  ret=$((ret + $?))

  ip -netns $ioam_node_alpha \
    ioam namespace show 2>/dev/null | grep -wq ${ALPHA[7]}
  ret=$((ret + $?))

  [ $ret == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_ioam_schema()
{
  ##############################################################################
  # Make sure the creation of an IOAM Schema works as expected.                #
  ##############################################################################
  local desc="Create an IOAM Schema"

  [ ! -z $1 ] && return

  ip -netns $ioam_node_alpha \
    ioam schema show 2>/dev/null | grep -wq ${ALPHA[8]}
  local ret=$?

  local sc_data=$(
    for i in `seq 0 $((${#ALPHA[9]}-1))`
    do
      chr=${ALPHA[9]:i:1}
      printf "%x " "'${chr}"
    done
  )

  ip -netns $ioam_node_alpha \
    ioam schema show 2>/dev/null | grep -q "$sc_data"
  ret=$((ret + $?))

  [ $ret == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_ioam_schema_namespace()
{
  ##############################################################################
  # Make sure the binding of a Schema to a Namespace works as expected.        #
  ##############################################################################
  local desc="Bind an IOAM Schema to an IOAM Namespace"

  [ ! -z $1 ] && return

  ip -netns $ioam_node_alpha \
    ioam namespace show 2>/dev/null | grep -wq ${ALPHA[8]}
  local ret=$?

  ip -netns $ioam_node_alpha \
    ioam schema show 2>/dev/null | grep -wq 123
  ret=$((ret + $?))

  [ $ret == 0 ] && log_test_passed "${desc}" || log_test_failed "${desc}"
}

local_route_ns()
{
  ##############################################################################
  # Make sure the Namespace-ID is always provided, whatever the mode.          #
  ##############################################################################
  local desc="Mandatory Namespace-ID"
  local mode

  [ -z $1 ] && return

  [ "$1" == "encap" ] && mode="$1 tundst 2001:db8:2::2" || mode="$1"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret1=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret2=$?

  [[ $ret1 == 0 || $ret2 != 0 ]] && log_test_failed "${desc}" \
                                 || log_test_passed "${desc}"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_tunsrc()
{
  ##############################################################################
  # Make sure the Tunnel Source is only (and possibly) used with encap mode.   #
  ##############################################################################
  local desc
  local mode
  local mode_tunsrc

  [ -z $1 ] && return

  if [ "$1" == "encap" ]
  then
    desc="Optional Tunnel Source"
    mode="$1 tundst 2001:db8:2::2"
    mode_tunsrc="$1 tunsrc 2001:db8:1::50 tundst 2001:db8:2::2"
  else
    desc="Unneeded Tunnel Source"
    mode="$1"
    mode_tunsrc="$1 tunsrc 2001:db8:1::50"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret1=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode_tunsrc trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret2=$?

  if [ "$1" == "encap" ]
  then
    [[ $ret1 != 0 || $ret2 != 0 ]] && log_test_failed "${desc}" \
                                   || log_test_passed "${desc}"
  else
    [[ $ret1 != 0 || $ret2 == 0 ]] && log_test_failed "${desc}" \
                                   || log_test_passed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_tundst()
{
  ##############################################################################
  # Make sure the Tunnel Destination is only (and always) used with encap mode.#
  ##############################################################################
  local desc

  [ -z $1 ] && return

  [ "$1" == "encap" ] && desc="Mandatory Tunnel Destination" \
                     || desc="Unneeded Tunnel Destination"

  local mode="$1"
  local mode_tundst="$1 tundst 2001:db8:2::2"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret1=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode_tundst trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret2=$?

  if [ "$1" == "encap" ]
  then
    [[ $ret1 == 0 || $ret2 != 0 ]] && log_test_failed "${desc}" \
                                   || log_test_passed "${desc}"
  else
    [[ $ret1 != 0 || $ret2 == 0 ]] && log_test_failed "${desc}" \
                                   || log_test_passed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_trace_type()
{
  ##############################################################################
  # Make sure the Trace Type is always provided, whatever the mode.            #
  ##############################################################################
  local desc="Mandatory Trace Type"
  local mode

  [ -z $1 ] && return

  [ "$1" == "encap" ] && mode="$1 tundst 2001:db8:2::2" || mode="$1"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret1=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret2=$?

  [[ $ret1 == 0 || $ret2 != 0 ]] && log_test_failed "${desc}" \
                                 || log_test_passed "${desc}"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_trace_size()
{
  ##############################################################################
  # Make sure the Trace Size is always provided, whatever the mode.            #
  ##############################################################################
  local desc="Mandatory Trace Size"
  local mode

  [ -z $1 ] && return

  [ "$1" == "encap" ] && mode="$1 tundst 2001:db8:2::2" || mode="$1"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret1=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size 4 \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  local ret2=$?

  [[ $ret1 == 0 || $ret2 != 0 ]] && log_test_failed "${desc}" \
                                 || log_test_passed "${desc}"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_trace_type_bits()
{
  ##############################################################################
  # Make sure only allowed bits (0-11 and 22) are accepted.                    #
  ##############################################################################
  local desc="Trace Type bits"
  local mode

  [ -z $1 ] && return

  [ "$1" == "encap" ] && mode="$1 tundst 2001:db8:2::2" || mode="$1"

  local i
  for i in {0..23}
  do
    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc type ${bit2type[$i]} ns 0 size 4 \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [[ ($? == 0 && (($i -ge 12 && $i -le 21) || $i == 23)) ||
          ($? != 0 && (($i -ge 0 && $i -le 11) || $i == 22)) ]]
    then
      local err=1
      break
    fi
  done

  [ -z $err ] && log_test_passed "${desc}" || log_test_failed "${desc}"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}

local_route_trace_size_values()
{
  ##############################################################################
  # Make sure only allowed sizes (multiples of four in [4,244]) are accepted.  #
  ##############################################################################
  local desc="Trace Size values"
  local mode

  [ -z $1 ] && return

  [ "$1" == "encap" ] && mode="$1 tundst 2001:db8:2::2" || mode="$1"

  # we also try the next multiple of four after the MAX to check it's refused
  local i
  for i in {0..248}
  do
    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc type 0x800000 ns 0 size $i \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [[ ($? == 0 && ($i == 0 || $i == 248 || $(( $i % 4 )) != 0)) ||
          ($? != 0 && $i != 0 && $i != 248 && $(( $i % 4 )) == 0) ]]
    then
      local err=1
      break
    fi
  done

  [ -z $err ] && log_test_passed "${desc}" || log_test_failed "${desc}"

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null
}


################################################################################
#                                                                              #
#                                 OUTPUT tests                                 #
#                                                                              #
################################################################################

output_undef_ns()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node does NOT fill the trace when the      #
  # corresponding IOAM Namespace-ID is not configured locally.                 #
  ##############################################################################
  local desc="Unknown IOAM Namespace-ID"
  local ns=0
  local tr_type=0x800000
  local tr_size=4
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" $saddr $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

output_no_room()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node does NOT fill the trace AND sets the  #
  # Overflow flag when there is not enough room for its data.                  #
  ##############################################################################
  local desc="Missing room for data"
  local ns=123
  local tr_type=0xc00000
  local tr_size=4
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" $saddr $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

output_no_room_oss()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node does NOT fill the trace AND sets the  #
  # Overflow flag when there is not enough room for the Opaque State Snapshot. #
  ##############################################################################
  local desc="Missing room for Opaque State Snapshot"
  local ns=123
  local tr_type=0x000002
  local tr_size=4
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" $saddr $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

output_bits()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node implements all supported bits by      #
  # checking it correctly fills the trace with its data.                       #
  ##############################################################################
  local desc="Trace Type with supported bit <n> only"
  local ns=123
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local tmp=${bit2size[22]}
  bit2size[22]=$(( $tmp + ${#ALPHA[9]} + ((4 - (${#ALPHA[9]} % 4)) % 4) ))

  local i
  for i in {0..11} {22..22}
  do
    local descr="${desc/<n>/$i}"

    if [[ "$1" == "encap" && $encap_tests != 0 ]]
    then
      log_test_skipped "${descr}"
      continue
    fi

    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc \
      type ${bit2type[$i]} ns $ns size ${bit2size[$i]} \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [ $? == 0 ]
    then
      run_test "output_bit$i" "${descr}" $saddr \
        ${bit2type[$i]} ${bit2size[$i]} $ns $1
    else
      log_test_failed "${descr}"
    fi
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null

  bit2size[22]=$tmp
}

output_sizes()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node allocates supported sizes correctly.  #
  ##############################################################################
  local desc="Trace Size of <n> bytes"
  local ns=0
  local tr_type=0x800000
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local i
  for i in $(seq 4 4 244)
  do
    local descr="${desc/<n>/$i}"

    if [[ "$1" == "encap" && $encap_tests != 0 ]]
    then
      log_test_skipped "${descr}"
      continue
    fi

    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $i \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [ $? == 0 ]
    then
      run_test "output_size$i" "${descr}" $saddr $tr_type $i $ns $1
    else
      log_test_failed "${descr}"
    fi
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

output_full_supp_trace()
{
  ##############################################################################
  # Make sure an IOAM encapsulating node correctly fills a trace when all      #
  # supported bits are set.                                                    #
  ##############################################################################
  local desc="Full supported trace"
  local ns=123
  local tr_type=0xfff002
  local tr_size
  local mode="$1"
  local saddr="2001:db8:1::2"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    if [ "$2" == "tunsrc" ]
    then
      saddr="2001:db8:1::50"
      mode+=" tunsrc 2001:db8:1::50"
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local i
  tr_size=$(( ${#ALPHA[9]} + ((4 - (${#ALPHA[9]} % 4)) % 4) ))
  for i in {0..11} {22..22}
  do
    tr_size=$((tr_size + bit2size[$i]))
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" $saddr $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}


################################################################################
#                                                                              #
#                                 INPUT tests                                  #
#                                                                              #
################################################################################

input_undef_ns()
{
  ##############################################################################
  # Make sure an IOAM node does NOT fill the trace when the corresponding IOAM #
  # Namespace-ID is not configured locally.                                    #
  ##############################################################################
  local desc="Unknown IOAM Namespace-ID"
  local ns=0
  local tr_type=0x800000
  local tr_size=4
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_no_room()
{
  ##############################################################################
  # Make sure an IOAM node does NOT fill the trace AND sets the Overflow flag  #
  # when there is not enough room for its data.                                #
  ##############################################################################
  local desc="Missing room for data"
  local ns=123
  local tr_type=0xc00000
  local tr_size=4
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_no_room_oss()
{
  ##############################################################################
  # Make sure an IOAM node does NOT fill the trace AND sets the Overflow flag  #
  # when there is not enough room for the Opaque State Snapshot.               #
  ##############################################################################
  local desc="Missing room for Opaque State Snapshot"
  local ns=123
  local tr_type=0x000002
  local tr_size=4
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_disabled()
{
  ##############################################################################
  # Make sure an IOAM node does NOT fill the trace when IOAM is not enabled on #
  # the corresponding (ingress) interface.                                     #
  ##############################################################################
  local desc="IOAM disabled on ingress interface"
  local ns=123
  local tr_type=0x800000
  local tr_size=4
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  # Exception: disable IOAM on ingress interface
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=0 &>/dev/null
  local ret=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  ret=$((ret + $?))

  if [ $ret == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  # Clean Exception
  ip netns exec $ioam_node_beta \
    sysctl -wq net.ipv6.conf.veth0.ioam6_enabled=1 &>/dev/null

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_oflag()
{
  ##############################################################################
  # Make sure an IOAM node does NOT fill the trace when the Overflow flag is   #
  # set.                                                                       #
  ##############################################################################
  local desc="Overflow flag is set"
  local ns=123
  local tr_type=0xc00000
  local tr_size=4
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  # Exception:
  #   Here, we need the sender to set the Overflow flag. For that, we will add
  #   back the IOAM namespace that was previously configured on the sender.
  ip -netns $ioam_node_alpha ioam namespace add 123 &>/dev/null
  local ret=$?

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null
  ret=$((ret + $?))

  if [ $ret == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  # Clean Exception
  ip -netns $ioam_node_alpha ioam namespace del 123 &>/dev/null

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_bits()
{
  ##############################################################################
  # Make sure an IOAM node implements all supported bits by checking it        #
  # correctly fills the trace with its data.                                   #
  ##############################################################################
  local desc="Trace Type with supported bit <n> only"
  local ns=123
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local tmp=${bit2size[22]}
  bit2size[22]=$(( $tmp + ${#BETA[9]} + ((4 - (${#BETA[9]} % 4)) % 4) ))

  local i
  for i in {0..11} {22..22}
  do
    local descr="${desc/<n>/$i}"

    if [[ "$1" == "encap" && $encap_tests != 0 ]]
    then
      log_test_skipped "${descr}"
      continue
    fi

    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc \
      type ${bit2type[$i]} ns $ns size ${bit2size[$i]} \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [ $? == 0 ]
    then
      run_test "input_bit$i" "${descr}" 2001:db8:1::2 \
        ${bit2type[$i]} ${bit2size[$i]} $ns $1
    else
      log_test_failed "${descr}"
    fi
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null

  bit2size[22]=$tmp
}

input_sizes()
{
  ##############################################################################
  # Make sure an IOAM node handles all supported sizes correctly.              #
  ##############################################################################
  local desc="Trace Size of <n> bytes"
  local ns=123
  local tr_type=0x800000
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local i
  for i in $(seq 4 4 244)
  do
    local descr="${desc/<n>/$i}"

    if [[ "$1" == "encap" && $encap_tests != 0 ]]
    then
      log_test_skipped "${descr}"
      continue
    fi

    ip -netns $ioam_node_alpha \
      route change 2001:db8:2::/64 \
      encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $i \
      via 2001:db8:1::1 dev veth0 &>/dev/null

    if [ $? == 0 ]
    then
      run_test "input_size$i" "${descr}" 2001:db8:1::2 $tr_type $i $ns $1
    else
      log_test_failed "${descr}"
    fi
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}

input_full_supp_trace()
{
  ##############################################################################
  # Make sure an IOAM node correctly fills a trace when all supported bits are #
  # set.                                                                       #
  ##############################################################################
  local desc="Full supported trace"
  local ns=123
  local tr_type=0xfff002
  local tr_size
  local mode="$1"

  if [ "$1" == "encap" ]
  then
    if [ $encap_tests != 0 ]
    then
      log_test_skipped "${desc}"
      return
    fi

    mode+=" tundst 2001:db8:2::2"
    ip -netns $ioam_node_gamma link set ip6tnl0 up &>/dev/null
  fi

  local i
  tr_size=$(( ${#BETA[9]} + ((4 - (${#BETA[9]} % 4)) % 4) ))
  for i in {0..11} {22..22}
  do
    tr_size=$((tr_size + bit2size[$i]))
  done

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 \
    encap ioam6 mode $mode trace prealloc type $tr_type ns $ns size $tr_size \
    via 2001:db8:1::1 dev veth0 &>/dev/null

  if [ $? == 0 ]
  then
    run_test ${FUNCNAME[0]} "${desc}" 2001:db8:1::2 $tr_type $tr_size $ns $1
  else
    log_test_failed "${desc}"
  fi

  ip -netns $ioam_node_alpha \
    route change 2001:db8:2::/64 via 2001:db8:1::1 dev veth0 &>/dev/null

  [ "$1" == "encap" ] && ip -netns $ioam_node_gamma \
    link set ip6tnl0 down &>/dev/null
}


################################################################################
#                                                                              #
#                                     MAIN                                     #
#                                                                              #
################################################################################

npassed=0
nskipped=0
nfailed=0

if [ "$(id -u)" -ne 0 ]
then
  echo "SKIP: Need root privileges."
  exit $ksft_skip
fi

if [ ! -x "$(command -v ip)" ]
then
  echo "SKIP: Could not run test without ip tool."
  exit $ksft_skip
fi

check_kernel_compatibility
setup
run
cleanup

if [ $nfailed != 0 ]
then
  exit $ksft_fail
fi

exit $ksft_pass
