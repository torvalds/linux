#!/usr/bin/env python3
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License
#
# Author: Octavian Purdila <tavi@cs.pub.ro>
#
# Based on TAP13:
#
# Copyright 2013, Red Hat, Inc.
# Author: Josef Skladanka <jskladan@redhat.com>
#

import re
import sys
import yamlish


class Reporter(object):

    def start(self, obj):
        None

    def end(self, obj):
        None


class Test(object):
    def __init__(self, reporter, result, id, description=None, directive=None,
                 comment=None):
        self.reporter = reporter
        self.result = result
        if directive:
            self.result = directive.lower()
        if id:
            self.id = int(id)
        else:
            self.id = None
        if description:
            self.description = description
        else:
            self.description = ""
        if comment:
            self.comment = "# " + comment
        else:
            self.comment = ""
        self.yaml = None
        self._yaml_buffer = None
        self.diagnostics = []

        self.reporter.start(self)

    def end(self):
        if not self.yaml:
            self.yaml = yamlish.load(self._yaml_buffer)
            self.reporter.end(self)


class Suite(object):
    def __init__(self, reporter, start, end, explanation):
        self.reporter = reporter
        self.tests = []
        self.name = explanation
        self.tests_planned = int(end)

        self.__tests_counter = 0
        self.__tests_base = 0

        self.reporter.start(self)

    def newTest(self, args):
        try:
            self.tests[-1].end()
        except IndexError:
            None

        if 'id' not in args or not args['id']:
            args['id'] = self.__tests_counter
        else:
            args['id'] = int(args['id']) + self.__tests_base

        if args['id'] < self.__tests_counter:
            print("error: bad test id %d, fixing it" % (args['id']))
            args['id'] = self.__tests_counter
        # according to TAP13 specs, missing tests must be handled as 'not ok'
        # here we add the missing tests in sequence
        while args['id'] > (self.__tests_counter + 1):
            comment = 'test %d not present' % self.__tests_counter
            self.tests.append(Test(self.reporter, 'not ok',
                                   self.__tests_counter, comment=comment))
            self.__tests_counter += 1

        if args['id'] == self.__tests_counter:
            if args['directive']:
                self.test().result = args['directive'].lower()
            else:
                self.test().result = args['result']
            self.reporter.start(self.test())
        else:
            self.tests.append(Test(self.reporter, **args))
            self.__tests_counter += 1

    def test(self):
        return self.tests[-1]

    def end(self, name, planned):
        if name == self.name:
            self.tests_planned += int(planned)
            self.__tests_base = self.__tests_counter
            return False
        try:
            self.test().end()
        except IndexError:
            None
        if len(self.tests) != self.tests_planned:
            for i in range(len(self.tests), self.tests_planned):
                self.tests.append(Test(self.reporter, 'not ok', i+1,
                                       comment='test not present'))
        return True


class Run(object):

    def __init__(self, reporter):
        self.reporter = reporter
        self.suites = []

    def suite(self):
        return self.suites[-1]

    def test(self):
        return self.suites[-1].tests[-1]

    def newSuite(self, args):
        new = False
        try:
            if self.suite().end(args['explanation'], args['end']):
                new = True
        except IndexError:
            new = True
        if new:
            self.suites.append(Suite(self.reporter, **args))

    def newTest(self, args):
        self.suite().newTest(args)


class Parser(object):
    RE_PLAN = re.compile(r"^\s*(?P<start>\d+)\.\.(?P<end>\d+)\s*(#\s*(?P<explanation>.*))?\s*$")
    RE_TEST_LINE = re.compile(r"^\s*(?P<result>(not\s+)?ok|[*]+)\s*(?P<id>\d+)?\s*(?P<description>[^#]+)?\s*(#\s*(?P<directive>TODO|SKIP)?\s*(?P<comment>.+)?)?\s*$",  re.IGNORECASE)
    RE_EXPLANATION = re.compile(r"^\s*#\s*(?P<explanation>.+)?\s*$")
    RE_YAMLISH_START = re.compile(r"^\s*---.*$")
    RE_YAMLISH_END = re.compile(r"^\s*\.\.\.\s*$")

    def __init__(self, reporter):
        self.seek_test = False
        self.in_test = False
        self.in_yaml = False
        self.run = Run(reporter)

    def parse(self, source):
        # to avoid input buffering
        while True:
            line = source.readline().decode('ascii')
            if not line:
                break

            if self.in_yaml:
                if Parser.RE_YAMLISH_END.match(line):
                    self.run.test()._yaml_buffer.append(line.strip())
                    self.in_yaml = False
                else:
                    self.run.test()._yaml_buffer.append(line.rstrip())
                continue

            line = line.strip()

            if self.in_test:
                if Parser.RE_EXPLANATION.match(line):
                    self.run.test().diagnostics.append(line)
                    continue
                if Parser.RE_YAMLISH_START.match(line):
                    self.run.test()._yaml_buffer = [line.strip()]
                    self.in_yaml = True
                    continue

            m = Parser.RE_PLAN.match(line)
            if m:
                self.seek_test = True
                args = m.groupdict()
                self.run.newSuite(args)
                continue

            if self.seek_test:
                m = Parser.RE_TEST_LINE.match(line)
                if m:
                    args = m.groupdict()
                    self.run.newTest(args)
                    self.in_test = True
                    continue

            print(line)
        try:
            self.run.suite().end(None, 0)
        except IndexError:
            None
