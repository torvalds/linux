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
args = parser.parse_args()

device = args.device
dir = args.dir
file_prefix = args.operation + "_"
num_filters = args.num_filters
num_files = args.num_files
operation = args.operation
handle = 1

for i in range(num_files):
    file = dir + '/' + file_prefix + str(i)
    os.system("./tdc_batch.py -n {} -a {} -e {} -m {} {} {}".format(
        num_filters, handle, operation, i, device, file))
    handle += num_filters
