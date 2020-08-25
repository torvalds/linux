#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

./tdc.py -c actions --nobuildebpf
./tdc.py -c qdisc
