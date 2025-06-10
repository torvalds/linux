#!/usr/bin/python
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Basic sanity check of perf JSON output as specified in the man page.

import argparse
import sys
import json

ap = argparse.ArgumentParser()
ap.add_argument('--no-args', action='store_true')
ap.add_argument('--interval', action='store_true')
ap.add_argument('--system-wide-no-aggr', action='store_true')
ap.add_argument('--system-wide', action='store_true')
ap.add_argument('--event', action='store_true')
ap.add_argument('--per-core', action='store_true')
ap.add_argument('--per-thread', action='store_true')
ap.add_argument('--per-cache', action='store_true')
ap.add_argument('--per-cluster', action='store_true')
ap.add_argument('--per-die', action='store_true')
ap.add_argument('--per-node', action='store_true')
ap.add_argument('--per-socket', action='store_true')
ap.add_argument('--metric-only', action='store_true')
ap.add_argument('--file', type=argparse.FileType('r'), default=sys.stdin)
args = ap.parse_args()

Lines = args.file.readlines()

def isfloat(num):
  try:
    float(num)
    return True
  except ValueError:
    return False


def isint(num):
  try:
    int(num)
    return True
  except ValueError:
    return False

def is_counter_value(num):
  return isfloat(num) or num == '<not counted>' or num == '<not supported>'

def check_json_output(expected_items):
  checks = {
      'aggregate-number': lambda x: isfloat(x),
      'core': lambda x: True,
      'counter-value': lambda x: is_counter_value(x),
      'cgroup': lambda x: True,
      'cpu': lambda x: isint(x),
      'cache': lambda x: True,
      'cluster': lambda x: True,
      'die': lambda x: True,
      'event': lambda x: True,
      'event-runtime': lambda x: isfloat(x),
      'interval': lambda x: isfloat(x),
      'metric-unit': lambda x: True,
      'metric-value': lambda x: isfloat(x),
      'metric-threshold': lambda x: x in ['unknown', 'good', 'less good', 'nearly bad', 'bad'],
      'metricgroup': lambda x: True,
      'node': lambda x: True,
      'pcnt-running': lambda x: isfloat(x),
      'socket': lambda x: True,
      'thread': lambda x: True,
      'unit': lambda x: True,
      'insn per cycle': lambda x: isfloat(x),
      'GHz': lambda x: True,  # FIXME: it seems unintended for --metric-only
  }
  input = '[\n' + ','.join(Lines) + '\n]'
  for item in json.loads(input):
    if expected_items != -1:
      count = len(item)
      if count not in expected_items and count >= 1 and count <= 7 and 'metric-value' in item:
        # Events that generate >1 metric may have isolated metric
        # values and possibly other prefixes like interval, core,
        # aggregate-number, or event-runtime/pcnt-running from multiplexing.
        pass
      elif count not in expected_items and count >= 1 and count <= 5 and 'metricgroup' in item:
        pass
      elif count - 1 in expected_items and 'metric-threshold' in item:
          pass
      elif count in expected_items and 'insn per cycle' in item:
          pass
      elif count not in expected_items:
        raise RuntimeError(f'wrong number of fields. counted {count} expected {expected_items}'
                           f' in \'{item}\'')
    for key, value in item.items():
      if key not in checks:
        raise RuntimeError(f'Unexpected key: key={key} value={value}')
      if not checks[key](value):
        raise RuntimeError(f'Check failed for: key={key} value={value}')


try:
  if args.no_args or args.system_wide or args.event:
    expected_items = [5, 7]
  elif args.interval or args.per_thread or args.system_wide_no_aggr:
    expected_items = [6, 8]
  elif args.per_core or args.per_socket or args.per_node or args.per_die or args.per_cluster or args.per_cache:
    expected_items = [7, 9]
  elif args.metric_only:
    expected_items = [1, 2]
  else:
    # If no option is specified, don't check the number of items.
    expected_items = -1
  check_json_output(expected_items)
except:
  print('Test failed for input:\n' + '\n'.join(Lines))
  raise
