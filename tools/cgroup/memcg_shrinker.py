#!/usr/bin/env python3
#
# Copyright (C) 2022 Roman Gushchin <roman.gushchin@linux.dev>
# Copyright (C) 2022 Meta

import os
import argparse
import sys


def scan_cgroups(cgroup_root):
    cgroups = {}

    for root, subdirs, _ in os.walk(cgroup_root):
        for cgroup in subdirs:
            path = os.path.join(root, cgroup)
            ino = os.stat(path).st_ino
            cgroups[ino] = path

    # (memcg ino, path)
    return cgroups


def scan_shrinkers(shrinker_debugfs):
    shrinkers = []

    for root, subdirs, _ in os.walk(shrinker_debugfs):
        for shrinker in subdirs:
            count_path = os.path.join(root, shrinker, "count")
            with open(count_path) as f:
                for line in f.readlines():
                    items = line.split(' ')
                    ino = int(items[0])
                    # (count, shrinker, memcg ino)
                    shrinkers.append((int(items[1]), shrinker, ino))
    return shrinkers


def main():
    parser = argparse.ArgumentParser(description='Display biggest shrinkers')
    parser.add_argument('-n', '--lines', type=int, help='Number of lines to print')

    args = parser.parse_args()

    cgroups = scan_cgroups("/sys/fs/cgroup/")
    shrinkers = scan_shrinkers("/sys/kernel/debug/shrinker/")
    shrinkers = sorted(shrinkers, reverse = True, key = lambda x: x[0])

    n = 0
    for s in shrinkers:
        count, name, ino = (s[0], s[1], s[2])
        if count == 0:
            break

        if ino == 0 or ino == 1:
            cg = "/"
        else:
            try:
                cg = cgroups[ino]
            except KeyError:
                cg = "unknown (%d)" % ino

        print("%-8s %-20s %s" % (count, name, cg))

        n += 1
        if args.lines and n >= args.lines:
            break


if __name__ == '__main__':
    main()
