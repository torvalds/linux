#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""Convert directories of JSON events to C code."""
import argparse
import csv
from functools import lru_cache
import json
import metric
import os
import sys
from typing import (Callable, Dict, Optional, Sequence, Set, Tuple)
import collections

# Global command line arguments.
_args = None
# List of regular event tables.
_event_tables = []
# List of event tables generated from "/sys" directories.
_sys_event_tables = []
# List of regular metric tables.
_metric_tables = []
# List of metric tables generated from "/sys" directories.
_sys_metric_tables = []
# Mapping between sys event table names and sys metric table names.
_sys_event_table_to_metric_table_mapping = {}
# Map from an event name to an architecture standard
# JsonEvent. Architecture standard events are in json files in the top
# f'{_args.starting_dir}/{_args.arch}' directory.
_arch_std_events = {}
# Events to write out when the table is closed
_pending_events = []
# Name of events table to be written out
_pending_events_tblname = None
# Metrics to write out when the table is closed
_pending_metrics = []
# Name of metrics table to be written out
_pending_metrics_tblname = None
# Global BigCString shared by all structures.
_bcs = None
# Map from the name of a metric group to a description of the group.
_metricgroups = {}
# Order specific JsonEvent attributes will be visited.
_json_event_attributes = [
    # cmp_sevent related attributes.
    'name', 'topic', 'desc',
    # Seems useful, put it early.
    'event',
    # Short things in alphabetical order.
    'compat', 'deprecated', 'perpkg', 'unit',
    # Longer things (the last won't be iterated over during decompress).
    'long_desc'
]

# Attributes that are in pmu_metric rather than pmu_event.
_json_metric_attributes = [
    'metric_name', 'metric_group', 'metric_expr', 'metric_threshold',
    'desc', 'long_desc', 'unit', 'compat', 'metricgroup_no_group',
    'default_metricgroup_name', 'aggr_mode', 'event_grouping'
]
# Attributes that are bools or enum int values, encoded as '0', '1',...
_json_enum_attributes = ['aggr_mode', 'deprecated', 'event_grouping', 'perpkg']

def removesuffix(s: str, suffix: str) -> str:
  """Remove the suffix from a string

  The removesuffix function is added to str in Python 3.9. We aim for 3.6
  compatibility and so provide our own function here.
  """
  return s[0:-len(suffix)] if s.endswith(suffix) else s


def file_name_to_table_name(prefix: str, parents: Sequence[str],
                            dirname: str) -> str:
  """Generate a C table name from directory names."""
  tblname = prefix
  for p in parents:
    tblname += '_' + p
  tblname += '_' + dirname
  return tblname.replace('-', '_')


def c_len(s: str) -> int:
  """Return the length of s a C string

  This doesn't handle all escape characters properly. It first assumes
  all \\ are for escaping, it then adjusts as it will have over counted
  \\. The code uses \000 rather than \0 as a terminator as an adjacent
  number would be folded into a string of \0 (ie. "\0" + "5" doesn't
  equal a terminator followed by the number 5 but the escape of
  \05). The code adjusts for \000 but not properly for all octal, hex
  or unicode values.
  """
  try:
    utf = s.encode(encoding='utf-8',errors='strict')
  except:
    print(f'broken string {s}')
    raise
  return len(utf) - utf.count(b'\\') + utf.count(b'\\\\') - (utf.count(b'\\000') * 2)

class BigCString:
  """A class to hold many strings concatenated together.

  Generating a large number of stand-alone C strings creates a large
  number of relocations in position independent code. The BigCString
  is a helper for this case. It builds a single string which within it
  are all the other C strings (to avoid memory issues the string
  itself is held as a list of strings). The offsets within the big
  string are recorded and when stored to disk these don't need
  relocation. To reduce the size of the string further, identical
  strings are merged. If a longer string ends-with the same value as a
  shorter string, these entries are also merged.
  """
  strings: Set[str]
  big_string: Sequence[str]
  offsets: Dict[str, int]
  insert_number: int
  insert_point: Dict[str, int]
  metrics: Set[str]

  def __init__(self):
    self.strings = set()
    self.insert_number = 0;
    self.insert_point = {}
    self.metrics = set()

  def add(self, s: str, metric: bool) -> None:
    """Called to add to the big string."""
    if s not in self.strings:
      self.strings.add(s)
      self.insert_point[s] = self.insert_number
      self.insert_number += 1
      if metric:
        self.metrics.add(s)

  def compute(self) -> None:
    """Called once all strings are added to compute the string and offsets."""

    folded_strings = {}
    # Determine if two strings can be folded, ie. let 1 string use the
    # end of another. First reverse all strings and sort them.
    sorted_reversed_strings = sorted([x[::-1] for x in self.strings])

    # Strings 'xyz' and 'yz' will now be [ 'zy', 'zyx' ]. Scan forward
    # for each string to see if there is a better candidate to fold it
    # into, in the example rather than using 'yz' we can use'xyz' at
    # an offset of 1. We record which string can be folded into which
    # in folded_strings, we don't need to record the offset as it is
    # trivially computed from the string lengths.
    for pos,s in enumerate(sorted_reversed_strings):
      best_pos = pos
      for check_pos in range(pos + 1, len(sorted_reversed_strings)):
        if sorted_reversed_strings[check_pos].startswith(s):
          best_pos = check_pos
        else:
          break
      if pos != best_pos:
        folded_strings[s[::-1]] = sorted_reversed_strings[best_pos][::-1]

    # Compute reverse mappings for debugging.
    fold_into_strings = collections.defaultdict(set)
    for key, val in folded_strings.items():
      if key != val:
        fold_into_strings[val].add(key)

    # big_string_offset is the current location within the C string
    # being appended to - comments, etc. don't count. big_string is
    # the string contents represented as a list. Strings are immutable
    # in Python and so appending to one causes memory issues, while
    # lists are mutable.
    big_string_offset = 0
    self.big_string = []
    self.offsets = {}

    def string_cmp_key(s: str) -> Tuple[bool, int, str]:
      return (s in self.metrics, self.insert_point[s], s)

    # Emit all strings that aren't folded in a sorted manner.
    for s in sorted(self.strings, key=string_cmp_key):
      if s not in folded_strings:
        self.offsets[s] = big_string_offset
        self.big_string.append(f'/* offset={big_string_offset} */ "')
        self.big_string.append(s)
        self.big_string.append('"')
        if s in fold_into_strings:
          self.big_string.append(' /* also: ' + ', '.join(fold_into_strings[s]) + ' */')
        self.big_string.append('\n')
        big_string_offset += c_len(s)
        continue

    # Compute the offsets of the folded strings.
    for s in folded_strings.keys():
      assert s not in self.offsets
      folded_s = folded_strings[s]
      self.offsets[s] = self.offsets[folded_s] + c_len(folded_s) - c_len(s)

_bcs = BigCString()

class JsonEvent:
  """Representation of an event loaded from a json file dictionary."""

  def __init__(self, jd: dict):
    """Constructor passed the dictionary of parsed json values."""

    def llx(x: int) -> str:
      """Convert an int to a string similar to a printf modifier of %#llx."""
      return str(x) if x >= 0 and x < 10 else hex(x)

    def fixdesc(s: str) -> str:
      """Fix formatting issue for the desc string."""
      if s is None:
        return None
      return removesuffix(removesuffix(removesuffix(s, '.  '),
                                       '. '), '.').replace('\n', '\\n').replace(
                                           '\"', '\\"').replace('\r', '\\r')

    def convert_aggr_mode(aggr_mode: str) -> Optional[str]:
      """Returns the aggr_mode_class enum value associated with the JSON string."""
      if not aggr_mode:
        return None
      aggr_mode_to_enum = {
          'PerChip': '1',
          'PerCore': '2',
      }
      return aggr_mode_to_enum[aggr_mode]

    def convert_metric_constraint(metric_constraint: str) -> Optional[str]:
      """Returns the metric_event_groups enum value associated with the JSON string."""
      if not metric_constraint:
        return None
      metric_constraint_to_enum = {
          'NO_GROUP_EVENTS': '1',
          'NO_GROUP_EVENTS_NMI': '2',
          'NO_NMI_WATCHDOG': '2',
          'NO_GROUP_EVENTS_SMT': '3',
      }
      return metric_constraint_to_enum[metric_constraint]

    def lookup_msr(num: str) -> Optional[str]:
      """Converts the msr number, or first in a list to the appropriate event field."""
      if not num:
        return None
      msrmap = {
          0x3F6: 'ldlat=',
          0x1A6: 'offcore_rsp=',
          0x1A7: 'offcore_rsp=',
          0x3F7: 'frontend=',
      }
      return msrmap[int(num.split(',', 1)[0], 0)]

    def real_event(name: str, event: str) -> Optional[str]:
      """Convert well known event names to an event string otherwise use the event argument."""
      fixed = {
          'inst_retired.any': 'event=0xc0,period=2000003',
          'inst_retired.any_p': 'event=0xc0,period=2000003',
          'cpu_clk_unhalted.ref': 'event=0x0,umask=0x03,period=2000003',
          'cpu_clk_unhalted.thread': 'event=0x3c,period=2000003',
          'cpu_clk_unhalted.core': 'event=0x3c,period=2000003',
          'cpu_clk_unhalted.thread_any': 'event=0x3c,any=1,period=2000003',
      }
      if not name:
        return None
      if name.lower() in fixed:
        return fixed[name.lower()]
      return event

    def unit_to_pmu(unit: str) -> Optional[str]:
      """Convert a JSON Unit to Linux PMU name."""
      if not unit:
        return 'default_core'
      # Comment brought over from jevents.c:
      # it's not realistic to keep adding these, we need something more scalable ...
      table = {
          'CBO': 'uncore_cbox',
          'QPI LL': 'uncore_qpi',
          'SBO': 'uncore_sbox',
          'iMPH-U': 'uncore_arb',
          'CPU-M-CF': 'cpum_cf',
          'CPU-M-SF': 'cpum_sf',
          'PAI-CRYPTO' : 'pai_crypto',
          'PAI-EXT' : 'pai_ext',
          'UPI LL': 'uncore_upi',
          'hisi_sicl,cpa': 'hisi_sicl,cpa',
          'hisi_sccl,ddrc': 'hisi_sccl,ddrc',
          'hisi_sccl,hha': 'hisi_sccl,hha',
          'hisi_sccl,l3c': 'hisi_sccl,l3c',
          'imx8_ddr': 'imx8_ddr',
          'imx9_ddr': 'imx9_ddr',
          'L3PMC': 'amd_l3',
          'DFPMC': 'amd_df',
          'UMCPMC': 'amd_umc',
          'cpu_core': 'cpu_core',
          'cpu_atom': 'cpu_atom',
          'ali_drw': 'ali_drw',
          'arm_cmn': 'arm_cmn',
          'tool': 'tool',
      }
      return table[unit] if unit in table else f'uncore_{unit.lower()}'

    def is_zero(val: str) -> bool:
        try:
            if val.startswith('0x'):
                return int(val, 16) == 0
            else:
                return int(val) == 0
        except e:
            return False

    def canonicalize_value(val: str) -> str:
        try:
            if val.startswith('0x'):
                return llx(int(val, 16))
            return str(int(val))
        except e:
            return val

    eventcode = 0
    if 'EventCode' in jd:
      eventcode = int(jd['EventCode'].split(',', 1)[0], 0)
    if 'ExtSel' in jd:
      eventcode |= int(jd['ExtSel']) << 8
    configcode = int(jd['ConfigCode'], 0) if 'ConfigCode' in jd else None
    eventidcode = int(jd['EventidCode'], 0) if 'EventidCode' in jd else None
    self.name = jd['EventName'].lower() if 'EventName' in jd else None
    self.topic = ''
    self.compat = jd.get('Compat')
    self.desc = fixdesc(jd.get('BriefDescription'))
    self.long_desc = fixdesc(jd.get('PublicDescription'))
    precise = jd.get('PEBS')
    msr = lookup_msr(jd.get('MSRIndex'))
    msrval = jd.get('MSRValue')
    extra_desc = ''
    if 'Data_LA' in jd:
      extra_desc += '  Supports address when precise'
      if 'Errata' in jd:
        extra_desc += '.'
    if 'Errata' in jd:
      extra_desc += '  Spec update: ' + jd['Errata']
    self.pmu = unit_to_pmu(jd.get('Unit'))
    filter = jd.get('Filter')
    self.unit = jd.get('ScaleUnit')
    self.perpkg = jd.get('PerPkg')
    self.aggr_mode = convert_aggr_mode(jd.get('AggregationMode'))
    self.deprecated = jd.get('Deprecated')
    self.metric_name = jd.get('MetricName')
    self.metric_group = jd.get('MetricGroup')
    self.metricgroup_no_group = jd.get('MetricgroupNoGroup')
    self.default_metricgroup_name = jd.get('DefaultMetricgroupName')
    self.event_grouping = convert_metric_constraint(jd.get('MetricConstraint'))
    self.metric_expr = None
    if 'MetricExpr' in jd:
      self.metric_expr = metric.ParsePerfJson(jd['MetricExpr']).Simplify()
    # Note, the metric formula for the threshold isn't parsed as the &
    # and > have incorrect precedence.
    self.metric_threshold = jd.get('MetricThreshold')

    arch_std = jd.get('ArchStdEvent')
    if precise and self.desc and '(Precise Event)' not in self.desc:
      extra_desc += ' (Must be precise)' if precise == '2' else (' (Precise '
                                                                 'event)')
    event = None
    if configcode is not None:
      event = f'config={llx(configcode)}'
    elif eventidcode is not None:
      event = f'eventid={llx(eventidcode)}'
    else:
      event = f'event={llx(eventcode)}'
    event_fields = [
        ('AnyThread', 'any='),
        ('PortMask', 'ch_mask='),
        ('CounterMask', 'cmask='),
        ('EdgeDetect', 'edge='),
        ('FCMask', 'fc_mask='),
        ('Invert', 'inv='),
        ('SampleAfterValue', 'period='),
        ('UMask', 'umask='),
        ('NodeType', 'type='),
        ('RdWrMask', 'rdwrmask='),
        ('EnAllCores', 'enallcores='),
        ('EnAllSlices', 'enallslices='),
        ('SliceId', 'sliceid='),
        ('ThreadMask', 'threadmask='),
    ]
    for key, value in event_fields:
      if key in jd and not is_zero(jd[key]):
        event += f',{value}{canonicalize_value(jd[key])}'
    if filter:
      event += f',{filter}'
    if msr:
      event += f',{msr}{msrval}'
    if self.desc and extra_desc:
      self.desc += extra_desc
    if self.long_desc and extra_desc:
      self.long_desc += extra_desc
    if arch_std:
      if arch_std.lower() in _arch_std_events:
        event = _arch_std_events[arch_std.lower()].event
        # Copy from the architecture standard event to self for undefined fields.
        for attr, value in _arch_std_events[arch_std.lower()].__dict__.items():
          if hasattr(self, attr) and not getattr(self, attr):
            setattr(self, attr, value)
      else:
        raise argparse.ArgumentTypeError('Cannot find arch std event:', arch_std)

    self.event = real_event(self.name, event)

  def __repr__(self) -> str:
    """String representation primarily for debugging."""
    s = '{\n'
    for attr, value in self.__dict__.items():
      if value:
        s += f'\t{attr} = {value},\n'
    return s + '}'

  def build_c_string(self, metric: bool) -> str:
    s = ''
    for attr in _json_metric_attributes if metric else _json_event_attributes:
      x = getattr(self, attr)
      if metric and x and attr == 'metric_expr':
        # Convert parsed metric expressions into a string. Slashes
        # must be doubled in the file.
        x = x.ToPerfJson().replace('\\', '\\\\')
      if metric and x and attr == 'metric_threshold':
        x = x.replace('\\', '\\\\')
      if attr in _json_enum_attributes:
        s += x if x else '0'
      else:
        s += f'{x}\\000' if x else '\\000'
    return s

  def to_c_string(self, metric: bool) -> str:
    """Representation of the event as a C struct initializer."""

    def fix_comment(s: str) -> str:
        return s.replace('*/', r'\*\/')

    s = self.build_c_string(metric)
    return f'{{ { _bcs.offsets[s] } }}, /* {fix_comment(s)} */\n'


@lru_cache(maxsize=None)
def read_json_events(path: str, topic: str) -> Sequence[JsonEvent]:
  """Read json events from the specified file."""
  try:
    events = json.load(open(path), object_hook=JsonEvent)
  except BaseException as err:
    print(f"Exception processing {path}")
    raise
  metrics: list[Tuple[str, str, metric.Expression]] = []
  for event in events:
    event.topic = topic
    if event.metric_name and '-' not in event.metric_name:
      metrics.append((event.pmu, event.metric_name, event.metric_expr))
  updates = metric.RewriteMetricsInTermsOfOthers(metrics)
  if updates:
    for event in events:
      if event.metric_name in updates:
        # print(f'Updated {event.metric_name} from\n"{event.metric_expr}"\n'
        #       f'to\n"{updates[event.metric_name]}"')
        event.metric_expr = updates[event.metric_name]

  return events

def preprocess_arch_std_files(archpath: str) -> None:
  """Read in all architecture standard events."""
  global _arch_std_events
  for item in os.scandir(archpath):
    if not item.is_file() or not item.name.endswith('.json'):
      continue
    try:
      for event in read_json_events(item.path, topic=''):
        if event.name:
          _arch_std_events[event.name.lower()] = event
        if event.metric_name:
          _arch_std_events[event.metric_name.lower()] = event
    except Exception as e:
        raise RuntimeError(f'Failure processing \'{item.name}\' in \'{archpath}\'') from e


def add_events_table_entries(item: os.DirEntry, topic: str) -> None:
  """Add contents of file to _pending_events table."""
  for e in read_json_events(item.path, topic):
    if e.name:
      _pending_events.append(e)
    if e.metric_name:
      _pending_metrics.append(e)


def print_pending_events() -> None:
  """Optionally close events table."""

  def event_cmp_key(j: JsonEvent) -> Tuple[str, str, bool, str, str]:
    def fix_none(s: Optional[str]) -> str:
      if s is None:
        return ''
      return s

    return (fix_none(j.pmu).replace(',','_'), fix_none(j.name), j.desc is not None, fix_none(j.topic),
            fix_none(j.metric_name))

  global _pending_events
  if not _pending_events:
    return

  global _pending_events_tblname
  if _pending_events_tblname.endswith('_sys'):
    global _sys_event_tables
    _sys_event_tables.append(_pending_events_tblname)
  else:
    global event_tables
    _event_tables.append(_pending_events_tblname)

  first = True
  last_pmu = None
  last_name = None
  pmus = set()
  for event in sorted(_pending_events, key=event_cmp_key):
    if last_pmu and last_pmu == event.pmu:
      assert event.name != last_name, f"Duplicate event: {last_pmu}/{last_name}/ in {_pending_events_tblname}"
    if event.pmu != last_pmu:
      if not first:
        _args.output_file.write('};\n')
      pmu_name = event.pmu.replace(',', '_')
      _args.output_file.write(
          f'static const struct compact_pmu_event {_pending_events_tblname}_{pmu_name}[] = {{\n')
      first = False
      last_pmu = event.pmu
      pmus.add((event.pmu, pmu_name))

    _args.output_file.write(event.to_c_string(metric=False))
    last_name = event.name
  _pending_events = []

  _args.output_file.write(f"""
}};

const struct pmu_table_entry {_pending_events_tblname}[] = {{
""")
  for (pmu, tbl_pmu) in sorted(pmus):
    pmu_name = f"{pmu}\\000"
    _args.output_file.write(f"""{{
     .entries = {_pending_events_tblname}_{tbl_pmu},
     .num_entries = ARRAY_SIZE({_pending_events_tblname}_{tbl_pmu}),
     .pmu_name = {{ {_bcs.offsets[pmu_name]} /* {pmu_name} */ }},
}},
""")
  _args.output_file.write('};\n\n')

def print_pending_metrics() -> None:
  """Optionally close metrics table."""

  def metric_cmp_key(j: JsonEvent) -> Tuple[bool, str, str]:
    def fix_none(s: Optional[str]) -> str:
      if s is None:
        return ''
      return s

    return (j.desc is not None, fix_none(j.pmu), fix_none(j.metric_name))

  global _pending_metrics
  if not _pending_metrics:
    return

  global _pending_metrics_tblname
  if _pending_metrics_tblname.endswith('_sys'):
    global _sys_metric_tables
    _sys_metric_tables.append(_pending_metrics_tblname)
  else:
    global metric_tables
    _metric_tables.append(_pending_metrics_tblname)

  first = True
  last_pmu = None
  pmus = set()
  for metric in sorted(_pending_metrics, key=metric_cmp_key):
    if metric.pmu != last_pmu:
      if not first:
        _args.output_file.write('};\n')
      pmu_name = metric.pmu.replace(',', '_')
      _args.output_file.write(
          f'static const struct compact_pmu_event {_pending_metrics_tblname}_{pmu_name}[] = {{\n')
      first = False
      last_pmu = metric.pmu
      pmus.add((metric.pmu, pmu_name))

    _args.output_file.write(metric.to_c_string(metric=True))
  _pending_metrics = []

  _args.output_file.write(f"""
}};

const struct pmu_table_entry {_pending_metrics_tblname}[] = {{
""")
  for (pmu, tbl_pmu) in sorted(pmus):
    pmu_name = f"{pmu}\\000"
    _args.output_file.write(f"""{{
     .entries = {_pending_metrics_tblname}_{tbl_pmu},
     .num_entries = ARRAY_SIZE({_pending_metrics_tblname}_{tbl_pmu}),
     .pmu_name = {{ {_bcs.offsets[pmu_name]} /* {pmu_name} */ }},
}},
""")
  _args.output_file.write('};\n\n')

def get_topic(topic: str) -> str:
  if topic.endswith('metrics.json'):
    return 'metrics'
  return removesuffix(topic, '.json').replace('-', ' ')

def preprocess_one_file(parents: Sequence[str], item: os.DirEntry) -> None:

  if item.is_dir():
    return

  # base dir or too deep
  level = len(parents)
  if level == 0 or level > 4:
    return

  # Ignore other directories. If the file name does not have a .json
  # extension, ignore it. It could be a readme.txt for instance.
  if not item.is_file() or not item.name.endswith('.json'):
    return

  if item.name == 'metricgroups.json':
    metricgroup_descriptions = json.load(open(item.path))
    for mgroup in metricgroup_descriptions:
      assert len(mgroup) > 1, parents
      description = f"{metricgroup_descriptions[mgroup]}\\000"
      mgroup = f"{mgroup}\\000"
      _bcs.add(mgroup, metric=True)
      _bcs.add(description, metric=True)
      _metricgroups[mgroup] = description
    return

  topic = get_topic(item.name)
  for event in read_json_events(item.path, topic):
    pmu_name = f"{event.pmu}\\000"
    if event.name:
      _bcs.add(pmu_name, metric=False)
      _bcs.add(event.build_c_string(metric=False), metric=False)
    if event.metric_name:
      _bcs.add(pmu_name, metric=True)
      _bcs.add(event.build_c_string(metric=True), metric=True)

def process_one_file(parents: Sequence[str], item: os.DirEntry) -> None:
  """Process a JSON file during the main walk."""
  def is_leaf_dir_ignoring_sys(path: str) -> bool:
    for item in os.scandir(path):
      if item.is_dir() and item.name != 'sys':
        return False
    return True

  # Model directories are leaves (ignoring possible sys
  # directories). The FTW will walk into the directory next. Flush
  # pending events and metrics and update the table names for the new
  # model directory.
  if item.is_dir() and is_leaf_dir_ignoring_sys(item.path):
    print_pending_events()
    print_pending_metrics()

    global _pending_events_tblname
    _pending_events_tblname = file_name_to_table_name('pmu_events_', parents, item.name)
    global _pending_metrics_tblname
    _pending_metrics_tblname = file_name_to_table_name('pmu_metrics_', parents, item.name)

    if item.name == 'sys':
      _sys_event_table_to_metric_table_mapping[_pending_events_tblname] = _pending_metrics_tblname
    return

  # base dir or too deep
  level = len(parents)
  if level == 0 or level > 4:
    return

  # Ignore other directories. If the file name does not have a .json
  # extension, ignore it. It could be a readme.txt for instance.
  if not item.is_file() or not item.name.endswith('.json') or item.name == 'metricgroups.json':
    return

  add_events_table_entries(item, get_topic(item.name))


def print_mapping_table(archs: Sequence[str]) -> None:
  """Read the mapfile and generate the struct from cpuid string to event table."""
  _args.output_file.write("""
/* Struct used to make the PMU event table implementation opaque to callers. */
struct pmu_events_table {
        const struct pmu_table_entry *pmus;
        uint32_t num_pmus;
};

/* Struct used to make the PMU metric table implementation opaque to callers. */
struct pmu_metrics_table {
        const struct pmu_table_entry *pmus;
        uint32_t num_pmus;
};

/*
 * Map a CPU to its table of PMU events. The CPU is identified by the
 * cpuid field, which is an arch-specific identifier for the CPU.
 * The identifier specified in tools/perf/pmu-events/arch/xxx/mapfile
 * must match the get_cpuid_str() in tools/perf/arch/xxx/util/header.c)
 *
 * The  cpuid can contain any character other than the comma.
 */
struct pmu_events_map {
        const char *arch;
        const char *cpuid;
        struct pmu_events_table event_table;
        struct pmu_metrics_table metric_table;
};

/*
 * Global table mapping each known CPU for the architecture to its
 * table of PMU events.
 */
const struct pmu_events_map pmu_events_map[] = {
""")
  for arch in archs:
    if arch == 'test':
      _args.output_file.write("""{
\t.arch = "testarch",
\t.cpuid = "testcpu",
\t.event_table = {
\t\t.pmus = pmu_events__test_soc_cpu,
\t\t.num_pmus = ARRAY_SIZE(pmu_events__test_soc_cpu),
\t},
\t.metric_table = {
\t\t.pmus = pmu_metrics__test_soc_cpu,
\t\t.num_pmus = ARRAY_SIZE(pmu_metrics__test_soc_cpu),
\t}
},
""")
    elif arch == 'common':
      _args.output_file.write("""{
\t.arch = "common",
\t.cpuid = "common",
\t.event_table = {
\t\t.pmus = pmu_events__common,
\t\t.num_pmus = ARRAY_SIZE(pmu_events__common),
\t},
\t.metric_table = {},
},
""")
    else:
      with open(f'{_args.starting_dir}/{arch}/mapfile.csv') as csvfile:
        table = csv.reader(csvfile)
        first = True
        for row in table:
          # Skip the first row or any row beginning with #.
          if not first and len(row) > 0 and not row[0].startswith('#'):
            event_tblname = file_name_to_table_name('pmu_events_', [], row[2].replace('/', '_'))
            if event_tblname in _event_tables:
              event_size = f'ARRAY_SIZE({event_tblname})'
            else:
              event_tblname = 'NULL'
              event_size = '0'
            metric_tblname = file_name_to_table_name('pmu_metrics_', [], row[2].replace('/', '_'))
            if metric_tblname in _metric_tables:
              metric_size = f'ARRAY_SIZE({metric_tblname})'
            else:
              metric_tblname = 'NULL'
              metric_size = '0'
            if event_size == '0' and metric_size == '0':
              continue
            cpuid = row[0].replace('\\', '\\\\')
            _args.output_file.write(f"""{{
\t.arch = "{arch}",
\t.cpuid = "{cpuid}",
\t.event_table = {{
\t\t.pmus = {event_tblname},
\t\t.num_pmus = {event_size}
\t}},
\t.metric_table = {{
\t\t.pmus = {metric_tblname},
\t\t.num_pmus = {metric_size}
\t}}
}},
""")
          first = False

  _args.output_file.write("""{
\t.arch = 0,
\t.cpuid = 0,
\t.event_table = { 0, 0 },
\t.metric_table = { 0, 0 },
}
};
""")


def print_system_mapping_table() -> None:
  """C struct mapping table array for tables from /sys directories."""
  _args.output_file.write("""
struct pmu_sys_events {
\tconst char *name;
\tstruct pmu_events_table event_table;
\tstruct pmu_metrics_table metric_table;
};

static const struct pmu_sys_events pmu_sys_event_tables[] = {
""")
  printed_metric_tables = []
  for tblname in _sys_event_tables:
    _args.output_file.write(f"""\t{{
\t\t.event_table = {{
\t\t\t.pmus = {tblname},
\t\t\t.num_pmus = ARRAY_SIZE({tblname})
\t\t}},""")
    metric_tblname = _sys_event_table_to_metric_table_mapping[tblname]
    if metric_tblname in _sys_metric_tables:
      _args.output_file.write(f"""
\t\t.metric_table = {{
\t\t\t.pmus = {metric_tblname},
\t\t\t.num_pmus = ARRAY_SIZE({metric_tblname})
\t\t}},""")
      printed_metric_tables.append(metric_tblname)
    _args.output_file.write(f"""
\t\t.name = \"{tblname}\",
\t}},
""")
  for tblname in _sys_metric_tables:
    if tblname in printed_metric_tables:
      continue
    _args.output_file.write(f"""\t{{
\t\t.metric_table = {{
\t\t\t.pmus = {tblname},
\t\t\t.num_pmus = ARRAY_SIZE({tblname})
\t\t}},
\t\t.name = \"{tblname}\",
\t}},
""")
  _args.output_file.write("""\t{
\t\t.event_table = { 0, 0 },
\t\t.metric_table = { 0, 0 },
\t},
};

static void decompress_event(int offset, struct pmu_event *pe)
{
\tconst char *p = &big_c_string[offset];
""")
  for attr in _json_event_attributes:
    _args.output_file.write(f'\n\tpe->{attr} = ')
    if attr in _json_enum_attributes:
      _args.output_file.write("*p - '0';\n")
    else:
      _args.output_file.write("(*p == '\\0' ? NULL : p);\n")
    if attr == _json_event_attributes[-1]:
      continue
    if attr in _json_enum_attributes:
      _args.output_file.write('\tp++;')
    else:
      _args.output_file.write('\twhile (*p++);')
  _args.output_file.write("""}

static void decompress_metric(int offset, struct pmu_metric *pm)
{
\tconst char *p = &big_c_string[offset];
""")
  for attr in _json_metric_attributes:
    _args.output_file.write(f'\n\tpm->{attr} = ')
    if attr in _json_enum_attributes:
      _args.output_file.write("*p - '0';\n")
    else:
      _args.output_file.write("(*p == '\\0' ? NULL : p);\n")
    if attr == _json_metric_attributes[-1]:
      continue
    if attr in _json_enum_attributes:
      _args.output_file.write('\tp++;')
    else:
      _args.output_file.write('\twhile (*p++);')
  _args.output_file.write("""}

static int pmu_events_table__for_each_event_pmu(const struct pmu_events_table *table,
                                                const struct pmu_table_entry *pmu,
                                                pmu_event_iter_fn fn,
                                                void *data)
{
        int ret;
        struct pmu_event pe = {
                .pmu = &big_c_string[pmu->pmu_name.offset],
        };

        for (uint32_t i = 0; i < pmu->num_entries; i++) {
                decompress_event(pmu->entries[i].offset, &pe);
                if (!pe.name)
                        continue;
                ret = fn(&pe, table, data);
                if (ret)
                        return ret;
        }
        return 0;
 }

static int pmu_events_table__find_event_pmu(const struct pmu_events_table *table,
                                            const struct pmu_table_entry *pmu,
                                            const char *name,
                                            pmu_event_iter_fn fn,
                                            void *data)
{
        struct pmu_event pe = {
                .pmu = &big_c_string[pmu->pmu_name.offset],
        };
        int low = 0, high = pmu->num_entries - 1;

        while (low <= high) {
                int cmp, mid = (low + high) / 2;

                decompress_event(pmu->entries[mid].offset, &pe);

                if (!pe.name && !name)
                        goto do_call;

                if (!pe.name && name) {
                        low = mid + 1;
                        continue;
                }
                if (pe.name && !name) {
                        high = mid - 1;
                        continue;
                }

                cmp = strcasecmp(pe.name, name);
                if (cmp < 0) {
                        low = mid + 1;
                        continue;
                }
                if (cmp > 0) {
                        high = mid - 1;
                        continue;
                }
  do_call:
                return fn ? fn(&pe, table, data) : 0;
        }
        return PMU_EVENTS__NOT_FOUND;
}

int pmu_events_table__for_each_event(const struct pmu_events_table *table,
                                    struct perf_pmu *pmu,
                                    pmu_event_iter_fn fn,
                                    void *data)
{
        for (size_t i = 0; i < table->num_pmus; i++) {
                const struct pmu_table_entry *table_pmu = &table->pmus[i];
                const char *pmu_name = &big_c_string[table_pmu->pmu_name.offset];
                int ret;

                if (pmu && !perf_pmu__name_wildcard_match(pmu, pmu_name))
                        continue;

                ret = pmu_events_table__for_each_event_pmu(table, table_pmu, fn, data);
                if (ret)
                        return ret;
        }
        return 0;
}

int pmu_events_table__find_event(const struct pmu_events_table *table,
                                 struct perf_pmu *pmu,
                                 const char *name,
                                 pmu_event_iter_fn fn,
                                 void *data)
{
        for (size_t i = 0; i < table->num_pmus; i++) {
                const struct pmu_table_entry *table_pmu = &table->pmus[i];
                const char *pmu_name = &big_c_string[table_pmu->pmu_name.offset];
                int ret;

                if (!perf_pmu__name_wildcard_match(pmu, pmu_name))
                        continue;

                ret = pmu_events_table__find_event_pmu(table, table_pmu, name, fn, data);
                if (ret != PMU_EVENTS__NOT_FOUND)
                        return ret;
        }
        return PMU_EVENTS__NOT_FOUND;
}

size_t pmu_events_table__num_events(const struct pmu_events_table *table,
                                    struct perf_pmu *pmu)
{
        size_t count = 0;

        for (size_t i = 0; i < table->num_pmus; i++) {
                const struct pmu_table_entry *table_pmu = &table->pmus[i];
                const char *pmu_name = &big_c_string[table_pmu->pmu_name.offset];

                if (perf_pmu__name_wildcard_match(pmu, pmu_name))
                        count += table_pmu->num_entries;
        }
        return count;
}

static int pmu_metrics_table__for_each_metric_pmu(const struct pmu_metrics_table *table,
                                                const struct pmu_table_entry *pmu,
                                                pmu_metric_iter_fn fn,
                                                void *data)
{
        int ret;
        struct pmu_metric pm = {
                .pmu = &big_c_string[pmu->pmu_name.offset],
        };

        for (uint32_t i = 0; i < pmu->num_entries; i++) {
                decompress_metric(pmu->entries[i].offset, &pm);
                if (!pm.metric_expr)
                        continue;
                ret = fn(&pm, table, data);
                if (ret)
                        return ret;
        }
        return 0;
}

int pmu_metrics_table__for_each_metric(const struct pmu_metrics_table *table,
                                     pmu_metric_iter_fn fn,
                                     void *data)
{
        for (size_t i = 0; i < table->num_pmus; i++) {
                int ret = pmu_metrics_table__for_each_metric_pmu(table, &table->pmus[i],
                                                                 fn, data);

                if (ret)
                        return ret;
        }
        return 0;
}

static const struct pmu_events_map *map_for_cpu(struct perf_cpu cpu)
{
        static struct {
                const struct pmu_events_map *map;
                struct perf_cpu cpu;
        } last_result;
        static struct {
                const struct pmu_events_map *map;
                char *cpuid;
        } last_map_search;
        static bool has_last_result, has_last_map_search;
        const struct pmu_events_map *map = NULL;
        char *cpuid = NULL;
        size_t i;

        if (has_last_result && last_result.cpu.cpu == cpu.cpu)
                return last_result.map;

        cpuid = get_cpuid_allow_env_override(cpu);

        /*
         * On some platforms which uses cpus map, cpuid can be NULL for
         * PMUs other than CORE PMUs.
         */
        if (!cpuid)
                goto out_update_last_result;

        if (has_last_map_search && !strcmp(last_map_search.cpuid, cpuid)) {
                map = last_map_search.map;
                free(cpuid);
        } else {
                i = 0;
                for (;;) {
                        map = &pmu_events_map[i++];

                        if (!map->arch) {
                                map = NULL;
                                break;
                        }

                        if (!strcmp_cpuid_str(map->cpuid, cpuid))
                                break;
               }
               free(last_map_search.cpuid);
               last_map_search.cpuid = cpuid;
               last_map_search.map = map;
               has_last_map_search = true;
        }
out_update_last_result:
        last_result.cpu = cpu;
        last_result.map = map;
        has_last_result = true;
        return map;
}

static const struct pmu_events_map *map_for_pmu(struct perf_pmu *pmu)
{
        struct perf_cpu cpu = {-1};

        if (pmu)
                cpu = perf_cpu_map__min(pmu->cpus);
        return map_for_cpu(cpu);
}

const struct pmu_events_table *perf_pmu__find_events_table(struct perf_pmu *pmu)
{
        const struct pmu_events_map *map = map_for_pmu(pmu);

        if (!map)
                return NULL;

        if (!pmu)
                return &map->event_table;

        for (size_t i = 0; i < map->event_table.num_pmus; i++) {
                const struct pmu_table_entry *table_pmu = &map->event_table.pmus[i];
                const char *pmu_name = &big_c_string[table_pmu->pmu_name.offset];

                if (perf_pmu__name_wildcard_match(pmu, pmu_name))
                         return &map->event_table;
        }
        return NULL;
}

const struct pmu_metrics_table *pmu_metrics_table__find(void)
{
        struct perf_cpu cpu = {-1};
        const struct pmu_events_map *map = map_for_cpu(cpu);

        return map ? &map->metric_table : NULL;
}

const struct pmu_events_table *find_core_events_table(const char *arch, const char *cpuid)
{
        for (const struct pmu_events_map *tables = &pmu_events_map[0];
             tables->arch;
             tables++) {
                if (!strcmp(tables->arch, arch) && !strcmp_cpuid_str(tables->cpuid, cpuid))
                        return &tables->event_table;
        }
        return NULL;
}

const struct pmu_metrics_table *find_core_metrics_table(const char *arch, const char *cpuid)
{
        for (const struct pmu_events_map *tables = &pmu_events_map[0];
             tables->arch;
             tables++) {
                if (!strcmp(tables->arch, arch) && !strcmp_cpuid_str(tables->cpuid, cpuid))
                        return &tables->metric_table;
        }
        return NULL;
}

int pmu_for_each_core_event(pmu_event_iter_fn fn, void *data)
{
        for (const struct pmu_events_map *tables = &pmu_events_map[0];
             tables->arch;
             tables++) {
                int ret = pmu_events_table__for_each_event(&tables->event_table,
                                                           /*pmu=*/ NULL, fn, data);

                if (ret)
                        return ret;
        }
        return 0;
}

int pmu_for_each_core_metric(pmu_metric_iter_fn fn, void *data)
{
        for (const struct pmu_events_map *tables = &pmu_events_map[0];
             tables->arch;
             tables++) {
                int ret = pmu_metrics_table__for_each_metric(&tables->metric_table, fn, data);

                if (ret)
                        return ret;
        }
        return 0;
}

const struct pmu_events_table *find_sys_events_table(const char *name)
{
        for (const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];
             tables->name;
             tables++) {
                if (!strcmp(tables->name, name))
                        return &tables->event_table;
        }
        return NULL;
}

int pmu_for_each_sys_event(pmu_event_iter_fn fn, void *data)
{
        for (const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];
             tables->name;
             tables++) {
                int ret = pmu_events_table__for_each_event(&tables->event_table,
                                                           /*pmu=*/ NULL, fn, data);

                if (ret)
                        return ret;
        }
        return 0;
}

int pmu_for_each_sys_metric(pmu_metric_iter_fn fn, void *data)
{
        for (const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];
             tables->name;
             tables++) {
                int ret = pmu_metrics_table__for_each_metric(&tables->metric_table, fn, data);

                if (ret)
                        return ret;
        }
        return 0;
}
""")

def print_metricgroups() -> None:
  _args.output_file.write("""
static const int metricgroups[][2] = {
""")
  for mgroup in sorted(_metricgroups):
    description = _metricgroups[mgroup]
    _args.output_file.write(
        f'\t{{ {_bcs.offsets[mgroup]}, {_bcs.offsets[description]} }}, /* {mgroup} => {description} */\n'
    )
  _args.output_file.write("""
};

const char *describe_metricgroup(const char *group)
{
        int low = 0, high = (int)ARRAY_SIZE(metricgroups) - 1;

        while (low <= high) {
                int mid = (low + high) / 2;
                const char *mgroup = &big_c_string[metricgroups[mid][0]];
                int cmp = strcmp(mgroup, group);

                if (cmp == 0) {
                        return &big_c_string[metricgroups[mid][1]];
                } else if (cmp < 0) {
                        low = mid + 1;
                } else {
                        high = mid - 1;
                }
        }
        return NULL;
}
""")

def main() -> None:
  global _args

  def dir_path(path: str) -> str:
    """Validate path is a directory for argparse."""
    if os.path.isdir(path):
      return path
    raise argparse.ArgumentTypeError(f'\'{path}\' is not a valid directory')

  def ftw(path: str, parents: Sequence[str],
          action: Callable[[Sequence[str], os.DirEntry], None]) -> None:
    """Replicate the directory/file walking behavior of C's file tree walk."""
    for item in sorted(os.scandir(path), key=lambda e: e.name):
      if _args.model != 'all' and item.is_dir():
        # Check if the model matches one in _args.model.
        if len(parents) == _args.model.split(',')[0].count('/'):
          # We're testing the correct directory.
          item_path = '/'.join(parents) + ('/' if len(parents) > 0 else '') + item.name
          if 'test' not in item_path and 'common' not in item_path and item_path not in _args.model.split(','):
            continue
      try:
        action(parents, item)
      except Exception as e:
        raise RuntimeError(f'Action failure for \'{item.name}\' in {parents}') from e
      if item.is_dir():
        ftw(item.path, parents + [item.name], action)

  ap = argparse.ArgumentParser()
  ap.add_argument('arch', help='Architecture name like x86')
  ap.add_argument('model', help='''Select a model such as skylake to
reduce the code size.  Normally set to "all". For architectures like
ARM64 with an implementor/model, the model must include the implementor
such as "arm/cortex-a34".''',
                  default='all')
  ap.add_argument(
      'starting_dir',
      type=dir_path,
      help='Root of tree containing architecture directories containing json files'
  )
  ap.add_argument(
      'output_file', type=argparse.FileType('w', encoding='utf-8'), nargs='?', default=sys.stdout)
  _args = ap.parse_args()

  _args.output_file.write(f"""
/* SPDX-License-Identifier: GPL-2.0 */
/* THIS FILE WAS AUTOGENERATED BY jevents.py arch={_args.arch} model={_args.model} ! */
""")
  _args.output_file.write("""
#include <pmu-events/pmu-events.h>
#include "util/header.h"
#include "util/pmu.h"
#include <string.h>
#include <stddef.h>

struct compact_pmu_event {
        int offset;
};

struct pmu_table_entry {
        const struct compact_pmu_event *entries;
        uint32_t num_entries;
        struct compact_pmu_event pmu_name;
};

""")
  archs = []
  for item in os.scandir(_args.starting_dir):
    if not item.is_dir():
      continue
    if item.name == _args.arch or _args.arch == 'all' or item.name == 'test' or item.name == 'common':
      archs.append(item.name)

  if len(archs) < 2 and _args.arch != 'none':
    raise IOError(f'Missing architecture directory \'{_args.arch}\'')

  archs.sort()
  for arch in archs:
    arch_path = f'{_args.starting_dir}/{arch}'
    preprocess_arch_std_files(arch_path)
    ftw(arch_path, [], preprocess_one_file)

  _bcs.compute()
  _args.output_file.write('static const char *const big_c_string =\n')
  for s in _bcs.big_string:
    _args.output_file.write(s)
  _args.output_file.write(';\n\n')
  for arch in archs:
    arch_path = f'{_args.starting_dir}/{arch}'
    ftw(arch_path, [], process_one_file)
    print_pending_events()
    print_pending_metrics()

  print_mapping_table(archs)
  print_system_mapping_table()
  print_metricgroups()

if __name__ == '__main__':
  main()
