#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""Convert directories of JSON events to C code."""
import argparse
import csv
import json
import os
import sys
from typing import Callable
from typing import Sequence

# Global command line arguments.
_args = None
# List of event tables generated from "/sys" directories.
_sys_event_tables = []
# Map from an event name to an architecture standard
# JsonEvent. Architecture standard events are in json files in the top
# f'{_args.starting_dir}/{_args.arch}' directory.
_arch_std_events = {}
# Track whether an events table is currently being defined and needs closing.
_close_table = False


def removesuffix(s: str, suffix: str) -> str:
  """Remove the suffix from a string

  The removesuffix function is added to str in Python 3.9. We aim for 3.6
  compatibility and so provide our own function here.
  """
  return s[0:-len(suffix)] if s.endswith(suffix) else s


def file_name_to_table_name(parents: Sequence[str], dirname: str) -> str:
  """Generate a C table name from directory names."""
  tblname = 'pme'
  for p in parents:
    tblname += '_' + p
  tblname += '_' + dirname
  return tblname.replace('-', '_')


class JsonEvent:
  """Representation of an event loaded from a json file dictionary."""

  def __init__(self, jd: dict):
    """Constructor passed the dictionary of parsed json values."""

    def llx(x: int) -> str:
      """Convert an int to a string similar to a printf modifier of %#llx."""
      return '0' if x == 0 else hex(x)

    def fixdesc(s: str) -> str:
      """Fix formatting issue for the desc string."""
      if s is None:
        return None
      return removesuffix(removesuffix(removesuffix(s, '.  '),
                                       '. '), '.').replace('\n', '\\n').replace(
                                           '\"', '\\"').replace('\r', '\\r')

    def convert_aggr_mode(aggr_mode: str) -> str:
      """Returns the aggr_mode_class enum value associated with the JSON string."""
      if not aggr_mode:
        return None
      aggr_mode_to_enum = {
          'PerChip': '1',
          'PerCore': '2',
      }
      return aggr_mode_to_enum[aggr_mode]

    def lookup_msr(num: str) -> str:
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

    def real_event(name: str, event: str) -> str:
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

    def unit_to_pmu(unit: str) -> str:
      """Convert a JSON Unit to Linux PMU name."""
      if not unit:
        return None
      # Comment brought over from jevents.c:
      # it's not realistic to keep adding these, we need something more scalable ...
      table = {
          'CBO': 'uncore_cbox',
          'QPI LL': 'uncore_qpi',
          'SBO': 'uncore_sbox',
          'iMPH-U': 'uncore_arb',
          'CPU-M-CF': 'cpum_cf',
          'CPU-M-SF': 'cpum_sf',
          'UPI LL': 'uncore_upi',
          'hisi_sicl,cpa': 'hisi_sicl,cpa',
          'hisi_sccl,ddrc': 'hisi_sccl,ddrc',
          'hisi_sccl,hha': 'hisi_sccl,hha',
          'hisi_sccl,l3c': 'hisi_sccl,l3c',
          'imx8_ddr': 'imx8_ddr',
          'L3PMC': 'amd_l3',
          'DFPMC': 'amd_df',
          'cpu_core': 'cpu_core',
          'cpu_atom': 'cpu_atom',
      }
      return table[unit] if unit in table else f'uncore_{unit.lower()}'

    eventcode = 0
    if 'EventCode' in jd:
      eventcode = int(jd['EventCode'].split(',', 1)[0], 0)
    if 'ExtSel' in jd:
      eventcode |= int(jd['ExtSel']) << 8
    configcode = int(jd['ConfigCode'], 0) if 'ConfigCode' in jd else None
    self.name = jd['EventName'].lower() if 'EventName' in jd else None
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
    self.metric_constraint = jd.get('MetricConstraint')
    self.metric_expr = jd.get('MetricExpr')
    if self.metric_expr:
      self.metric_expr = self.metric_expr.replace('\\', '\\\\')
    arch_std = jd.get('ArchStdEvent')
    if precise and self.desc and not '(Precise Event)' in self.desc:
      extra_desc += ' (Must be precise)' if precise == '2' else (' (Precise '
                                                                 'event)')
    event = f'config={llx(configcode)}' if configcode is not None else f'event={llx(eventcode)}'
    event_fields = [
        ('AnyThread', 'any='),
        ('PortMask', 'ch_mask='),
        ('CounterMask', 'cmask='),
        ('EdgeDetect', 'edge='),
        ('FCMask', 'fc_mask='),
        ('Invert', 'inv='),
        ('SampleAfterValue', 'period='),
        ('UMask', 'umask='),
    ]
    for key, value in event_fields:
      if key in jd and jd[key] != '0':
        event += ',' + value + jd[key]
    if filter:
      event += f',{filter}'
    if msr:
      event += f',{msr}{msrval}'
    if self.desc and extra_desc:
      self.desc += extra_desc
    if self.long_desc and extra_desc:
      self.long_desc += extra_desc
    if self.pmu:
      if self.desc and not self.desc.endswith('. '):
        self.desc += '. '
      self.desc = (self.desc if self.desc else '') + ('Unit: ' + self.pmu + ' ')
    if arch_std and arch_std.lower() in _arch_std_events:
      event = _arch_std_events[arch_std.lower()].event
      # Copy from the architecture standard event to self for undefined fields.
      for attr, value in _arch_std_events[arch_std.lower()].__dict__.items():
        if hasattr(self, attr) and not getattr(self, attr):
          setattr(self, attr, value)

    self.event = real_event(self.name, event)

  def __repr__(self) -> str:
    """String representation primarily for debugging."""
    s = '{\n'
    for attr, value in self.__dict__.items():
      if value:
        s += f'\t{attr} = {value},\n'
    return s + '}'

  def to_c_string(self, topic_local: str) -> str:
    """Representation of the event as a C struct initializer."""

    def attr_string(attr: str, value: str) -> str:
      return '\t.%s = \"%s\",\n' % (attr, value)

    def str_if_present(self, attr: str) -> str:
      if not getattr(self, attr):
        return ''
      return attr_string(attr, getattr(self, attr))

    s = '{\n'
    for attr in ['name', 'event']:
      s += str_if_present(self, attr)
    if self.desc is not None:
      s += attr_string('desc', self.desc)
    else:
      s += attr_string('desc', '(null)')
    s += str_if_present(self, 'compat')
    s += f'\t.topic = "{topic_local}",\n'
    for attr in [
        'long_desc', 'pmu', 'unit', 'perpkg', 'aggr_mode', 'metric_expr',
        'metric_name', 'metric_group', 'deprecated', 'metric_constraint'
    ]:
      s += str_if_present(self, attr)
    s += '},\n'
    return s


def read_json_events(path: str) -> Sequence[JsonEvent]:
  """Read json events from the specified file."""
  return json.load(open(path), object_hook=lambda d: JsonEvent(d))


def preprocess_arch_std_files(archpath: str) -> None:
  """Read in all architecture standard events."""
  global _arch_std_events
  for item in os.scandir(archpath):
    if item.is_file() and item.name.endswith('.json'):
      for event in read_json_events(item.path):
        if event.name:
          _arch_std_events[event.name.lower()] = event


def print_events_table_prefix(tblname: str) -> None:
  """Called when a new events table is started."""
  global _close_table
  if _close_table:
    raise IOError('Printing table prefix but last table has no suffix')
  _args.output_file.write(f'static const struct pmu_event {tblname}[] = {{\n')
  _close_table = True


def print_events_table_entries(item: os.DirEntry, topic: str) -> None:
  """Create contents of an events table."""
  if not _close_table:
    raise IOError('Table entries missing prefix')
  for event in read_json_events(item.path):
    _args.output_file.write(event.to_c_string(topic))


def print_events_table_suffix() -> None:
  """Optionally close events table."""
  global _close_table
  if _close_table:
    _args.output_file.write("""{
\t.name = 0,
\t.event = 0,
\t.desc = 0,
},
};
""")
  _close_table = False


def process_one_file(parents: Sequence[str], item: os.DirEntry) -> None:
  """Process a JSON file during the main walk."""
  global _sys_event_tables

  def get_topic(topic: str) -> str:
    return removesuffix(topic, '.json').replace('-', ' ')

  def is_leaf_dir(path: str) -> bool:
    for item in os.scandir(path):
      if item.is_dir():
        return False
    return True

  # model directory, reset topic
  if item.is_dir() and is_leaf_dir(item.path):
    print_events_table_suffix()

    tblname = file_name_to_table_name(parents, item.name)
    if item.name == 'sys':
      _sys_event_tables.append(tblname)
    print_events_table_prefix(tblname)
    return

  # base dir or too deep
  level = len(parents)
  if level == 0 or level > 4:
    return

  # Ignore other directories. If the file name does not have a .json
  # extension, ignore it. It could be a readme.txt for instance.
  if not item.is_file() or not item.name.endswith('.json'):
    return

  print_events_table_entries(item, get_topic(item.name))


def print_mapping_table() -> None:
  """Read the mapfile and generate the struct from cpuid string to event table."""
  with open(f'{_args.starting_dir}/{_args.arch}/mapfile.csv') as csvfile:
    table = csv.reader(csvfile)
    _args.output_file.write(
        'const struct pmu_events_map pmu_events_map[] = {\n')
    first = True
    for row in table:
      # Skip the first row or any row beginning with #.
      if not first and len(row) > 0 and not row[0].startswith('#'):
        tblname = file_name_to_table_name([], row[2].replace('/', '_'))
        _args.output_file.write("""{
\t.cpuid = \"%s\",
\t.version = \"%s\",
\t.type = \"%s\",
\t.table = %s
},
""" % (row[0].replace('\\', '\\\\'), row[1], row[3], tblname))
      first = False

  _args.output_file.write("""{
\t.cpuid = "testcpu",
\t.version = "v1",
\t.type = "core",
\t.table = pme_test_soc_cpu,
},
{
\t.cpuid = 0,
\t.version = 0,
\t.type = 0,
\t.table = 0,
},
};
""")


def print_system_mapping_table() -> None:
  """C struct mapping table array for tables from /sys directories."""
  _args.output_file.write(
      '\nconst struct pmu_sys_events pmu_sys_event_tables[] = {\n')
  for tblname in _sys_event_tables:
    _args.output_file.write(f"""\t{{
\t\t.table = {tblname},
\t\t.name = \"{tblname}\",
\t}},
""")
  _args.output_file.write("""\t{
\t\t.table = 0
\t},
};
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
    for item in os.scandir(path):
      action(parents, item)
      if item.is_dir():
        ftw(item.path, parents + [item.name], action)

  ap = argparse.ArgumentParser()
  ap.add_argument('arch', help='Architecture name like x86')
  ap.add_argument(
      'starting_dir',
      type=dir_path,
      help='Root of tree containing architecture directories containing json files'
  )
  ap.add_argument(
      'output_file', type=argparse.FileType('w'), nargs='?', default=sys.stdout)
  _args = ap.parse_args()

  _args.output_file.write("#include \"pmu-events/pmu-events.h\"\n")
  for path in [_args.arch, 'test']:
    arch_path = f'{_args.starting_dir}/{path}'
    if not os.path.isdir(arch_path):
      raise IOError(f'Missing architecture directory in \'{arch_path}\'')
    preprocess_arch_std_files(arch_path)
    ftw(arch_path, [], process_one_file)
    print_events_table_suffix()

  print_mapping_table()
  print_system_mapping_table()


if __name__ == '__main__':
  main()
