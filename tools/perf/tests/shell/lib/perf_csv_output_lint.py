#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0

import argparse
import sys

# Basic sanity check of perf CSV output as specified in the man page.
# Currently just checks the number of fields per line in output.

ap = argparse.ArgumentParser()
ap.add_argument('--no-args', action='store_true')
ap.add_argument('--interval', action='store_true')
ap.add_argument('--system-wide-no-aggr', action='store_true')
ap.add_argument('--system-wide', action='store_true')
ap.add_argument('--event', action='store_true')
ap.add_argument('--per-core', action='store_true')
ap.add_argument('--per-thread', action='store_true')
ap.add_argument('--per-die', action='store_true')
ap.add_argument('--per-node', action='store_true')
ap.add_argument('--per-socket', action='store_true')
ap.add_argument('--separator', default=',', nargs='?')
args = ap.parse_args()

Lines = sys.stdin.readlines()

def check_csv_output(exp):
  for line in Lines:
    if 'failed' not in line:
      count = line.count(args.separator)
      if count != exp:
        sys.stdout.write(''.join(Lines))
        raise RuntimeError(f'wrong number of fields. expected {exp} in {line}')

try:
  if args.no_args or args.system_wide or args.event:
    expected_items = 6
  elif args.interval or args.per_thread or args.system_wide_no_aggr:
    expected_items = 7
  elif args.per_core or args.per_socket or args.per_node or args.per_die:
    expected_items = 8
  else:
    ap.print_help()
    raise RuntimeError('No checking option specified')
  check_csv_output(expected_items)

except:
  sys.stdout.write('Test failed for input: ' + ''.join(Lines))
  raise
