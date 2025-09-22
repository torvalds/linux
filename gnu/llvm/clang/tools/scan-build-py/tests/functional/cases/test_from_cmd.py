# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear
from . import make_args, check_call_and_report, create_empty_file
import unittest

import os
import os.path
import glob


class OutputDirectoryTest(unittest.TestCase):
    @staticmethod
    def run_analyzer(outdir, args, cmd):
        return check_call_and_report(
            ["scan-build-py", "--intercept-first", "-o", outdir] + args, cmd
        )

    def test_regular_keeps_report_dir(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_regular"]
            outdir = self.run_analyzer(tmpdir, [], make)
            self.assertTrue(os.path.isdir(outdir))

    def test_clear_deletes_report_dir(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_clean"]
            outdir = self.run_analyzer(tmpdir, [], make)
            self.assertFalse(os.path.isdir(outdir))

    def test_clear_keeps_report_dir_when_asked(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_clean"]
            outdir = self.run_analyzer(tmpdir, ["--keep-empty"], make)
            self.assertTrue(os.path.isdir(outdir))


class RunAnalyzerTest(unittest.TestCase):
    @staticmethod
    def get_plist_count(directory):
        return len(glob.glob(os.path.join(directory, "report-*.plist")))

    def test_interposition_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_regular"]
            outdir = check_call_and_report(
                ["scan-build-py", "--plist", "-o", tmpdir, "--override-compiler"], make
            )

            self.assertTrue(os.path.isdir(outdir))
            self.assertEqual(self.get_plist_count(outdir), 5)

    def test_intercept_wrapper_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_regular"]
            outdir = check_call_and_report(
                [
                    "scan-build-py",
                    "--plist",
                    "-o",
                    tmpdir,
                    "--intercept-first",
                    "--override-compiler",
                ],
                make,
            )

            self.assertTrue(os.path.isdir(outdir))
            self.assertEqual(self.get_plist_count(outdir), 5)

    def test_intercept_library_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            make = make_args(tmpdir) + ["build_regular"]
            outdir = check_call_and_report(
                ["scan-build-py", "--plist", "-o", tmpdir, "--intercept-first"], make
            )

            self.assertTrue(os.path.isdir(outdir))
            self.assertEqual(self.get_plist_count(outdir), 5)

    @staticmethod
    def compile_empty_source_file(target_dir, is_cxx):
        compiler = "$CXX" if is_cxx else "$CC"
        src_file_name = "test.cxx" if is_cxx else "test.c"
        src_file = os.path.join(target_dir, src_file_name)
        obj_file = os.path.join(target_dir, "test.o")
        create_empty_file(src_file)
        command = " ".join([compiler, "-c", src_file, "-o", obj_file])
        return ["sh", "-c", command]

    def test_interposition_cc_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            outdir = check_call_and_report(
                ["scan-build-py", "--plist", "-o", tmpdir, "--override-compiler"],
                self.compile_empty_source_file(tmpdir, False),
            )
            self.assertEqual(self.get_plist_count(outdir), 1)

    def test_interposition_cxx_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            outdir = check_call_and_report(
                ["scan-build-py", "--plist", "-o", tmpdir, "--override-compiler"],
                self.compile_empty_source_file(tmpdir, True),
            )
            self.assertEqual(self.get_plist_count(outdir), 1)

    def test_intercept_cc_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            outdir = check_call_and_report(
                [
                    "scan-build-py",
                    "--plist",
                    "-o",
                    tmpdir,
                    "--override-compiler",
                    "--intercept-first",
                ],
                self.compile_empty_source_file(tmpdir, False),
            )
            self.assertEqual(self.get_plist_count(outdir), 1)

    def test_intercept_cxx_works(self):
        with libear.TemporaryDirectory() as tmpdir:
            outdir = check_call_and_report(
                [
                    "scan-build-py",
                    "--plist",
                    "-o",
                    tmpdir,
                    "--override-compiler",
                    "--intercept-first",
                ],
                self.compile_empty_source_file(tmpdir, True),
            )
            self.assertEqual(self.get_plist_count(outdir), 1)
