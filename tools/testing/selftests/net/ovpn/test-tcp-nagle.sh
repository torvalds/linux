#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 OpenVPN, Inc.
#
#  Author:	Antonio Quartulli <antonio@openvpn.net>

# Peers running openvpn 2.7.0 to 2.7.5 leave Nagle enabled on their TCP
# sockets. Keep exercising that configuration so it does not silently break.
OVPN_PROTO="TCP"
export OVPN_TCP_NODELAY=0

source test.sh
