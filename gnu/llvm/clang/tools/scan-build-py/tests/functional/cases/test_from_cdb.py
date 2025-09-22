# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear
from . import call_and_report
import unittest

import os.path
import string
import glob


def prepare_cdb(name, target_dir):
    target_file = "build_{0}.json".format(name)
    this_dir, _ = os.path.split(__file__)
    path = os.path.abspath(os.path.join(this_dir, "..", "src"))
    source_dir = os.path.join(path, "compilation_database")
    source_file = os.path.join(source_dir, target_file + ".in")
    target_file = os.path.join(target_dir, "compile_commands.json")
    with open(source_file, "r") as in_handle:
        with open(target_file, "w") as out_handle:
            for line in in_handle:
                temp = string.Template(line)
                out_handle.write(temp.substitute(path=path))
    return target_file


def run_analyzer(directory, cdb, args):
    cmd = ["analyze-build", "--cdb", cdb, "--output", directory] + args
    return call_and_report(cmd, [])


class OutputDirectoryTest(unittest.TestCase):
    def test_regular_keeps_report_dir(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, [])
            self.assertTrue(os.path.isdir(reportdir))

    def test_clear_deletes_report_dir(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("clean", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, [])
            self.assertFalse(os.path.isdir(reportdir))

    def test_clear_keeps_report_dir_when_asked(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("clean", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--keep-empty"])
            self.assertTrue(os.path.isdir(reportdir))


class ExitCodeTest(unittest.TestCase):
    def test_regular_does_not_set_exit_code(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, [])
            self.assertFalse(exit_code)

    def test_clear_does_not_set_exit_code(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("clean", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, [])
            self.assertFalse(exit_code)

    def test_regular_sets_exit_code_if_asked(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, ["--status-bugs"])
            self.assertTrue(exit_code)

    def test_clear_does_not_set_exit_code_if_asked(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("clean", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, ["--status-bugs"])
            self.assertFalse(exit_code)

    def test_regular_sets_exit_code_if_asked_from_plist(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, ["--status-bugs", "--plist"])
            self.assertTrue(exit_code)

    def test_clear_does_not_set_exit_code_if_asked_from_plist(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("clean", tmpdir)
            exit_code, __ = run_analyzer(tmpdir, cdb, ["--status-bugs", "--plist"])
            self.assertFalse(exit_code)


class OutputFormatTest(unittest.TestCase):
    @staticmethod
    def get_html_count(directory):
        return len(glob.glob(os.path.join(directory, "report-*.html")))

    @staticmethod
    def get_plist_count(directory):
        return len(glob.glob(os.path.join(directory, "report-*.plist")))

    @staticmethod
    def get_sarif_count(directory):
        return len(glob.glob(os.path.join(directory, "result-*.sarif")))

    def test_default_only_creates_html_report(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, [])
            self.assertTrue(os.path.exists(os.path.join(reportdir, "index.html")))
            self.assertEqual(self.get_html_count(reportdir), 2)
            self.assertEqual(self.get_plist_count(reportdir), 0)
            self.assertEqual(self.get_sarif_count(reportdir), 0)

    def test_plist_and_html_creates_html_and_plist_reports(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--plist-html"])
            self.assertTrue(os.path.exists(os.path.join(reportdir, "index.html")))
            self.assertEqual(self.get_html_count(reportdir), 2)
            self.assertEqual(self.get_plist_count(reportdir), 5)
            self.assertEqual(self.get_sarif_count(reportdir), 0)

    def test_plist_only_creates_plist_report(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--plist"])
            self.assertFalse(os.path.exists(os.path.join(reportdir, "index.html")))
            self.assertEqual(self.get_html_count(reportdir), 0)
            self.assertEqual(self.get_plist_count(reportdir), 5)
            self.assertEqual(self.get_sarif_count(reportdir), 0)

    def test_sarif_only_creates_sarif_result(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--sarif"])
            self.assertFalse(os.path.exists(os.path.join(reportdir, "index.html")))
            self.assertTrue(
                os.path.exists(os.path.join(reportdir, "results-merged.sarif"))
            )
            self.assertEqual(self.get_html_count(reportdir), 0)
            self.assertEqual(self.get_plist_count(reportdir), 0)
            self.assertEqual(self.get_sarif_count(reportdir), 5)

    def test_sarif_and_html_creates_sarif_and_html_reports(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("regular", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--sarif-html"])
            self.assertTrue(os.path.exists(os.path.join(reportdir, "index.html")))
            self.assertTrue(
                os.path.exists(os.path.join(reportdir, "results-merged.sarif"))
            )
            self.assertEqual(self.get_html_count(reportdir), 2)
            self.assertEqual(self.get_plist_count(reportdir), 0)
            self.assertEqual(self.get_sarif_count(reportdir), 5)


class FailureReportTest(unittest.TestCase):
    def test_broken_creates_failure_reports(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("broken", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, [])
            self.assertTrue(os.path.isdir(os.path.join(reportdir, "failures")))

    def test_broken_does_not_creates_failure_reports(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("broken", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, ["--no-failure-reports"])
            self.assertFalse(os.path.isdir(os.path.join(reportdir, "failures")))


class TitleTest(unittest.TestCase):
    def assertTitleEqual(self, directory, expected):
        import re

        patterns = [
            re.compile(r"<title>(?P<page>.*)</title>"),
            re.compile(r"<h1>(?P<head>.*)</h1>"),
        ]
        result = dict()

        index = os.path.join(directory, "index.html")
        with open(index, "r") as handler:
            for line in handler.readlines():
                for regex in patterns:
                    match = regex.match(line.strip())
                    if match:
                        result.update(match.groupdict())
                        break
        self.assertEqual(result["page"], result["head"])
        self.assertEqual(result["page"], expected)

    def test_default_title_in_report(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("broken", tmpdir)
            exit_code, reportdir = run_analyzer(tmpdir, cdb, [])
            self.assertTitleEqual(reportdir, "src - analyzer results")

    def test_given_title_in_report(self):
        with libear.TemporaryDirectory() as tmpdir:
            cdb = prepare_cdb("broken", tmpdir)
            exit_code, reportdir = run_analyzer(
                tmpdir, cdb, ["--html-title", "this is the title"]
            )
            self.assertTitleEqual(reportdir, "this is the title")
