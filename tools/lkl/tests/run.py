#!/usr/bin/env python
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License
#
# Author: Octavian Purdila <tavi@cs.pub.ro>
#

from __future__ import print_function

import argparse
import os
import subprocess
import sys
import tap13
import xml.etree.ElementTree as ET

from junit_xml import TestSuite, TestCase


class Reporter(tap13.Reporter):
    def start(self, obj):
        if type(obj) is tap13.Test:
            if obj.result == "*":
                end='\r'
            else:
                end='\n'
            print("  TEST       %-8s %.50s" %
                  (obj.result, obj.description + " " + obj.comment), end=end)

        elif type(obj) is tap13.Suite:
            if obj.tests_planned == 0:
                status = "skip"
            else:
                status = ""
            print("  SUITE      %-8s %s" % (status, obj.name))

    def end(self, obj):
        if type(obj) is tap13.Test:
            if obj.result != "ok":
                try:
                    print(obj.yaml["log"], end='')
                except:
                    None


mydir=os.path.dirname(os.path.realpath(__file__))

tests = [
    'boot.sh',
    'disk.sh -t ext4',
    'disk.sh -t btrfs',
    'disk.sh -t vfat',
    'disk.sh -t xfs',
    'net.sh -b loopback',
    'net.sh -b tap',
    'net.sh -b pipe',
    'net.sh -b raw',
    'net.sh -b macvtap',
    'net.sh -b wintap',
    'lklfuse.sh -t ext4',
    'lklfuse.sh -t btrfs',
    'lklfuse.sh -t vfat',
    'lklfuse.sh -t xfs',
    'config',
    'hijack-test.sh',
    'disk-vfio-pci.sh -t ext4 run',
    'disk-vfio-pci.sh -t btrfs run',
    'disk-vfio-pci.sh -t vfat run',
    'disk-vfio-pci.sh -t xfs run'
]

parser = argparse.ArgumentParser(description='LKL test runner')
parser.add_argument('tests', nargs='?', action='append',
                    help='tests to run %s' % tests)
parser.add_argument('--junit-dir',
                    help='directory where to store the juni suites')
parser.add_argument('--gdb', action='store_true', default=False,
                    help='run simple tests under gdb; implies --pass-through')
parser.add_argument('--pass-through', action='store_true',  default=False,
                    help='run the test without interpeting the test output')
parser.add_argument('--valgrind', action='store_true', default=False,
                    help='run simple tests under valgrind')

args = parser.parse_args()
if args.tests == [None]:
    args.tests = tests

if args.gdb:
    args.pass_through=True
    os.environ['GDB']="yes"

if args.valgrind:
    os.environ['VALGRIND']="yes"

tap = tap13.Parser(Reporter())

os.environ['PATH'] += ":" + mydir

exit_code = 0

for t in args.tests:
    if not t:
        continue
    if args.pass_through:
        print(t)
        if subprocess.call(t, shell=True) != 0:
            exit_code = 1
    else:
        p = subprocess.Popen(t, shell=True, stdout=subprocess.PIPE)
        tap.parse(p.stdout)

if args.pass_through:
    sys.exit(exit_code)

suites_count = 0
tests_total = 0
tests_not_ok = 0
tests_ok = 0
tests_skip = 0
val_errs = 0
val_fails = 0
val_skips = 0

for s in tap.run.suites:

    junit_tests = []
    suites_count += 1

    for t in s.tests:
        try:
            secs = t.yaml["time_us"] / 1000000.0
        except:
            secs = 0
        try:
            log = t.yaml['log']
        except:
            log = ""

        jt = TestCase(t.description, elapsed_sec=secs, stdout=log)
        if t.result == 'skip':
            jt.add_skipped_info(output=log)
        elif t.result == 'not ok':
            jt.add_error_info(output=log)

        junit_tests.append(jt)

        tests_total += 1
        if t.result == "ok":
            tests_ok += 1
        elif t.result == "not ok":
            tests_not_ok += 1
            exit_code = 1
        elif t.result == "skip":
            tests_skip += 1

    if args.junit_dir:
        js = TestSuite(s.name, junit_tests)
        with open(os.path.join(args.junit_dir, os.path.basename(s.name) + '.xml'), 'w') as f:
            js.to_file(f, [js])

        if os.getenv('VALGRIND') is not None:
            val_xml = 'valgrind-%s.xml' % os.path.basename(s.name).replace(' ','-')
            # skipped tests don't generate xml file
            if os.path.exists(val_xml) is False:
                continue

            cmd = 'mv %s %s' % (val_xml, args.junit_dir)
            subprocess.call(cmd, shell=True, )

            cmd = mydir + '/valgrind2xunit.py ' + val_xml
            subprocess.call(cmd, shell=True, cwd=args.junit_dir)

            # count valgrind results
            doc = ET.parse(os.path.join(args.junit_dir, 'valgrind-%s_xunit.xml' \
                                        % (os.path.basename(s.name).replace(' ','-'))))
            ts = doc.getroot()
            val_errs += int(ts.get('errors'))
            val_fails += int(ts.get('failures'))
            val_skips += int(ts.get('skip'))

print("Summary: %d suites run, %d tests, %d ok, %d not ok, %d skipped" %
      (suites_count, tests_total, tests_ok, tests_not_ok, tests_skip))

if os.getenv('VALGRIND') is not None:
    print(" valgrind (memcheck): %d failures, %d skipped" % (val_fails, val_skips))
    if val_errs or val_fails:
        exit_code = 1

sys.exit(exit_code)
