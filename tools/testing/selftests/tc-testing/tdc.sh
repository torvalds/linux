#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

modprobe netdevsim
modprobe sch_teql
./tdc.py -c actions --nobuildebpf
./tdc.py -c qdisc
