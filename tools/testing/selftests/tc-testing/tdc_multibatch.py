#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
"""
tdc_multibatch.py - a thin wrapper over tdc_batch.py to generate multiple batch
files

Copyright (C) 2019 Vlad Buslov <vladbu@mellanox.com>
"""

import argparse
import os

parser = argparse.ArgumentParser(
    description='TC multiple batch file generator')
parser.add_argument("device", help="device name")
parser.add_argument("dir", help="where to put batch files")
parser.add_argument(
    "num_filters", type=int, help="how many lines per batch file")
parser.add_argument("num_files", type=int, help="how many batch files")
parser.add_argument(
    "operation",
    choices=['add', 'del', 'replace'],
    help="operation to perform on filters")
parser.add_argument(
    "-x",
    "--file_prefix",
    default="",
    help="prefix for generated batch file names")
parser.add_argument(
    "-d",
    "--duplicate_handles",
    action="store_true",
    help="duplicate filter handle range in all files")
parser.add_argument(
    "-a",
    "--handle_start",
    type=int,
    default=1,
    help="start handle range from (default: 1)")
parser.add_argument(
    "-m",
    "--mac_prefix",
    type=int,
    default=0,
    choices=range(0, 256),
    help="add this value to third byte of source MAC address of flower filter"
    "(default: 0)")
args = parser.parse_args()

device = args.device
dir = args.dir
file_prefix = args.file_prefix + args.operation + "_"
num_filters = args.num_filters
num_files = args.num_files
operation = args.operation
duplicate_handles = args.duplicate_handles
handle = args.handle_start
mac_prefix = args.mac_prefix

for i in range(num_files):
    file = dir + '/' + file_prefix + str(i)
    os.system("./tdc_batch.py -n {} -a {} -e {} -m {} {} {}".format(
        num_filters, handle, operation, i + mac_prefix, device, file))
    if not duplicate_handles:
        handle += num_filters
