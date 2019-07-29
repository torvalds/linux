#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# add private ipv4 and ipv6 addresses to loopback

readonly V6_INNER='100::a/128'
readonly V4_INNER='192.168.0.1/32'

if getopts ":s" opt; then
  readonly SIT_DEV_NAME='sixtofourtest0'
  readonly V6_SIT='2::/64'
  readonly V4_SIT='172.17.0.1/32'
  shift
fi

fail() {
  echo "error: $*" 1>&2
  exit 1
}

setup() {
  ip -6 addr add "${V6_INNER}" dev lo || fail 'failed to setup v6 address'
  ip -4 addr add "${V4_INNER}" dev lo || fail 'failed to setup v4 address'

  if [[ -n "${V6_SIT}" ]]; then
    ip link add "${SIT_DEV_NAME}" type sit remote any local any \
	    || fail 'failed to add sit'
    ip link set dev "${SIT_DEV_NAME}" up \
	    || fail 'failed to bring sit device up'
    ip -6 addr add "${V6_SIT}" dev "${SIT_DEV_NAME}" \
	    || fail 'failed to setup v6 SIT address'
    ip -4 addr add "${V4_SIT}" dev "${SIT_DEV_NAME}" \
	    || fail 'failed to setup v4 SIT address'
  fi

  sleep 2	# avoid race causing bind to fail
}

cleanup() {
  if [[ -n "${V6_SIT}" ]]; then
    ip -4 addr del "${V4_SIT}" dev "${SIT_DEV_NAME}"
    ip -6 addr del "${V6_SIT}" dev "${SIT_DEV_NAME}"
    ip link del "${SIT_DEV_NAME}"
  fi

  ip -4 addr del "${V4_INNER}" dev lo
  ip -6 addr del "${V6_INNER}" dev lo
}

trap cleanup EXIT

setup
"$@"
exit "$?"
