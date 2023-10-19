#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# setup tunnels for flow dissection test

readonly SUFFIX="test_$(mktemp -u XXXX)"
CONFIG="remote 127.0.0.2 local 127.0.0.1 dev lo"

setup() {
  ip link add "ipip_${SUFFIX}" type ipip ${CONFIG}
  ip link add "gre_${SUFFIX}" type gre ${CONFIG}
  ip link add "sit_${SUFFIX}" type sit ${CONFIG}

  echo "tunnels before test:"
  ip tunnel show

  ip link set "ipip_${SUFFIX}" up
  ip link set "gre_${SUFFIX}" up
  ip link set "sit_${SUFFIX}" up
}


cleanup() {
  ip tunnel del "ipip_${SUFFIX}"
  ip tunnel del "gre_${SUFFIX}"
  ip tunnel del "sit_${SUFFIX}"

  echo "tunnels after test:"
  ip tunnel show
}

trap cleanup EXIT

setup
"$@"
exit "$?"
