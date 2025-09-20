#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""List model names from mapfile.csv files."""
import argparse
import csv
import os
import re
from typing import List

def main() -> None:
    def dir_path(path: str) -> str:
        """Validate path is a directory for argparse."""
        if os.path.isdir(path):
            return path
        raise argparse.ArgumentTypeError(f'\'{path}\' is not a valid directory')

    def find_archs(start_dir: str, arch: str) -> List[str]:
        archs = []
        for item in os.scandir(start_dir):
            if not item.is_dir():
                continue
            if arch in (item.name, 'all'):
                archs.append(item.name)

        if len(archs) < 1:
            raise IOError(f'Missing architecture directory \'{arch}\'')

        return archs

    def find_mapfiles(start_dir: str, archs: List[str]) -> List[str]:
        result = []
        for arch in archs:
            for item in os.scandir(f'{start_dir}/{arch}'):
                if item.is_dir():
                    continue
                if item.name == 'mapfile.csv':
                    result.append(f'{start_dir}/{arch}/mapfile.csv')
        return result

    def find_cpuids(mapfiles: List[str], cpuids: str) -> List[str]:
        result = []
        for mapfile in mapfiles:
            with open(mapfile, encoding='utf-8') as csvfile:
                first = False
                table = csv.reader(csvfile)
                for row in table:
                    if not first or len(row) == 0 or row[0].startswith('#'):
                        first = True
                        continue
                    # Python regular expressions don't handle xdigit.
                    regex = row[0].replace('[[:xdigit:]]', '[0-9a-fA-F]')
                    for cpuid in cpuids.split(','):
                        if re.match(regex, cpuid):
                            result.append(row[2])
        return result

    ap = argparse.ArgumentParser()
    ap.add_argument('arch', help='Architecture name like x86')
    ap.add_argument('cpuid', default='all', help='List of cpuids to convert to model names')
    ap.add_argument(
        'starting_dir',
        type=dir_path,
        help='Root of tree containing architecture directories containing json files'
    )
    args = ap.parse_args()

    archs = find_archs(args.starting_dir, args.arch)
    mapfiles = find_mapfiles(args.starting_dir, archs)
    models = find_cpuids(mapfiles, args.cpuid)
    print(','.join(models))

if __name__ == '__main__':
    main()
