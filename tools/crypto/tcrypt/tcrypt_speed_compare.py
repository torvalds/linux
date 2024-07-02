#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) xFusion Digital Technologies Co., Ltd., 2023
#
# Author: Wang Jinchao <wangjinchao@xfusion.com>
#
"""
A tool for comparing tcrypt speed test logs.

Please note that for such a comparison, stability depends
on whether we allow frequency to float or pin the frequency.

Both support tests for operations within one second and
cycles of operation.
For example, use it in the bash script below.

```bash
#!/bin/bash

# log file prefix
seq_num=0

# When sec=0, it will perform cycle tests;
# otherwise, it indicates the duration of a single test
sec=0
num_mb=8
mode=211

# base speed test
lsmod | grep pcrypt && modprobe -r pcrypt
dmesg -C
modprobe tcrypt alg="pcrypt(rfc4106(gcm(aes)))" type=3
modprobe tcrypt mode=${mode} sec=${sec} num_mb=${num_mb}
dmesg > ${seq_num}_base_dmesg.log

# new speed test
lsmod | grep pcrypt && modprobe -r pcrypt
dmesg -C
modprobe tcrypt alg="pcrypt(rfc4106(gcm(aes)))" type=3
modprobe tcrypt mode=${mode} sec=${sec} num_mb=${num_mb}
dmesg > ${seq_num}_new_dmesg.log
lsmod | grep pcrypt && modprobe -r pcrypt

tools/crypto/tcrypt/tcrypt_speed_compare.py \
    ${seq_num}_base_dmesg.log \
    ${seq_num}_new_dmesg.log  \
        >${seq_num}_compare.log
grep 'average' -A2 -B0 --group-separator="" ${seq_num}_compare.log
```
"""

import sys
import re


def parse_title(line):
    pattern = r'tcrypt: testing speed of (.*?) (encryption|decryption)'
    match = re.search(pattern, line)
    if match:
        alg = match.group(1)
        op = match.group(2)
        return alg, op
    else:
        return "", ""


def parse_item(line):
    pattern_operations = r'\((\d+) bit key, (\d+) byte blocks\): (\d+) operations'
    pattern_cycles = r'\((\d+) bit key, (\d+) byte blocks\): 1 operation in (\d+) cycles'
    match = re.search(pattern_operations, line)
    if match:
        res = {
            "bit_key": int(match.group(1)),
            "byte_blocks": int(match.group(2)),
            "operations": int(match.group(3)),
        }
        return res

    match = re.search(pattern_cycles, line)
    if match:
        res = {
            "bit_key": int(match.group(1)),
            "byte_blocks": int(match.group(2)),
            "cycles": int(match.group(3)),
        }
        return res

    return None


def parse(filepath):
    result = {}
    alg, op = "", ""
    with open(filepath, 'r') as file:
        for line in file:
            if not line:
                continue
            _alg, _op = parse_title(line)
            if _alg:
                alg, op = _alg, _op
                if alg not in result:
                    result[alg] = {}
                if op not in result[alg]:
                    result[alg][op] = []
                continue
            parsed_result = parse_item(line)
            if parsed_result:
                result[alg][op].append(parsed_result)
    return result


def merge(base, new):
    merged = {}
    for alg in base.keys():
        merged[alg] = {}
        for op in base[alg].keys():
            if op not in merged[alg]:
                merged[alg][op] = []
            for index in range(len(base[alg][op])):
                merged_item = {
                    "bit_key": base[alg][op][index]["bit_key"],
                    "byte_blocks": base[alg][op][index]["byte_blocks"],
                }
                if "operations" in base[alg][op][index].keys():
                    merged_item["base_ops"] = base[alg][op][index]["operations"]
                    merged_item["new_ops"] = new[alg][op][index]["operations"]
                else:
                    merged_item["base_cycles"] = base[alg][op][index]["cycles"]
                    merged_item["new_cycles"] = new[alg][op][index]["cycles"]

                merged[alg][op].append(merged_item)
    return merged


def format(merged):
    for alg in merged.keys():
        for op in merged[alg].keys():
            base_sum = 0
            new_sum = 0
            differ_sum = 0
            differ_cnt = 0
            print()
            hlen = 80
            print("="*hlen)
            print(f"{alg}")
            print(f"{' '*(len(alg)//3) + op}")
            print("-"*hlen)
            key = ""
            if "base_ops" in merged[alg][op][0]:
                key = "ops"
                print(f"bit key | byte blocks | base ops    | new ops     | differ(%)")
            else:
                key = "cycles"
                print(f"bit key | byte blocks | base cycles | new cycles  | differ(%)")
            for index in range(len(merged[alg][op])):
                item = merged[alg][op][index]
                base_cnt = item[f"base_{key}"]
                new_cnt = item[f"new_{key}"]
                base_sum += base_cnt
                new_sum += new_cnt
                differ = round((new_cnt - base_cnt)*100/base_cnt, 2)
                differ_sum += differ
                differ_cnt += 1
                bit_key = item["bit_key"]
                byte_blocks = item["byte_blocks"]
                print(
                    f"{bit_key:<7} | {byte_blocks:<11} | {base_cnt:<11} | {new_cnt:<11} | {differ:<8}")
            average_speed_up = "{:.2f}".format(differ_sum/differ_cnt)
            ops_total_speed_up = "{:.2f}".format(
                (base_sum - new_sum) * 100 / base_sum)
            print('-'*hlen)
            print(f"average differ(%s)    | total_differ(%)")
            print('-'*hlen)
            print(f"{average_speed_up:<21} | {ops_total_speed_up:<10}")
            print('='*hlen)


def main(base_log, new_log):
    base = parse(base_log)
    new = parse(new_log)
    merged = merge(base, new)
    format(merged)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} base_log new_log")
        exit(-1)
    main(sys.argv[1], sys.argv[2])
