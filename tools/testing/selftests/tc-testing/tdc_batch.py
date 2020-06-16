#!/usr/bin/python3

"""
tdc_batch.py - a script to generate TC batch file

Copyright (C) 2017 Chris Mi <chrism@mellanox.com>
"""

import argparse

parser = argparse.ArgumentParser(description='TC batch file generator')
parser.add_argument("device", help="device name")
parser.add_argument("file", help="batch file name")
parser.add_argument("-n", "--number", type=int,
                    help="how many lines in batch file")
parser.add_argument(
    "-a",
    "--handle_start",
    type=int,
    default=1,
    help="start handle range from (default: 1)")
parser.add_argument("-o", "--skip_sw",
                    help="skip_sw (offload), by default skip_hw",
                    action="store_true")
parser.add_argument("-s", "--share_action",
                    help="all filters share the same action",
                    action="store_true")
parser.add_argument("-p", "--prio",
                    help="all filters have different prio",
                    action="store_true")
parser.add_argument(
    "-e",
    "--operation",
    choices=['add', 'del', 'replace'],
    default='add',
    help="operation to perform on filters"
    "(default: add filter)")
parser.add_argument(
    "-m",
    "--mac_prefix",
    type=int,
    default=0,
    choices=range(0, 256),
    help="third byte of source MAC address of flower filter"
    "(default: 0)")
args = parser.parse_args()

device = args.device
file = open(args.file, 'w')

number = 1
if args.number:
    number = args.number

handle_start = args.handle_start

skip = "skip_hw"
if args.skip_sw:
    skip = "skip_sw"

share_action = ""
if args.share_action:
    share_action = "index 1"

prio = "prio 1"
if args.prio:
    prio = ""
    if number > 0x4000:
        number = 0x4000

mac_prefix = args.mac_prefix

def format_add_filter(device, prio, handle, skip, src_mac, dst_mac,
                      share_action):
    return ("filter add dev {} {} protocol ip ingress handle {} "
            " flower {} src_mac {} dst_mac {} action drop {}".format(
                device, prio, handle, skip, src_mac, dst_mac, share_action))


def format_rep_filter(device, prio, handle, skip, src_mac, dst_mac,
                      share_action):
    return ("filter replace dev {} {} protocol ip ingress handle {} "
            " flower {} src_mac {} dst_mac {} action drop {}".format(
                device, prio, handle, skip, src_mac, dst_mac, share_action))


def format_del_filter(device, prio, handle, skip, src_mac, dst_mac,
                      share_action):
    return ("filter del dev {} {} protocol ip ingress handle {} "
            "flower".format(device, prio, handle))


formatter = format_add_filter
if args.operation == "del":
    formatter = format_del_filter
elif args.operation == "replace":
    formatter = format_rep_filter

index = 0
for i in range(0x100):
    for j in range(0x100):
        for k in range(0x100):
            mac = ("{:02x}:{:02x}:{:02x}".format(i, j, k))
            src_mac = "e4:11:{:02x}:{}".format(mac_prefix, mac)
            dst_mac = "e4:12:00:" + mac
            cmd = formatter(device, prio, handle_start + index, skip, src_mac,
                            dst_mac, share_action)
            file.write("{}\n".format(cmd))
            index += 1
            if index >= number:
                file.close()
                exit(0)
