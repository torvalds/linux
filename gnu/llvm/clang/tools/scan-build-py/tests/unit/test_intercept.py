# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import libear
import libscanbuild.intercept as sut
import unittest
import os.path


class InterceptUtilTest(unittest.TestCase):
    def test_format_entry_filters_action(self):
        def test(command):
            trace = {"command": command, "directory": "/opt/src/project"}
            return list(sut.format_entry(trace))

        self.assertTrue(test(["cc", "-c", "file.c", "-o", "file.o"]))
        self.assertFalse(test(["cc", "-E", "file.c"]))
        self.assertFalse(test(["cc", "-MM", "file.c"]))
        self.assertFalse(test(["cc", "this.o", "that.o", "-o", "a.out"]))

    def test_format_entry_normalize_filename(self):
        parent = os.path.join(os.sep, "home", "me")
        current = os.path.join(parent, "project")

        def test(filename):
            trace = {"directory": current, "command": ["cc", "-c", filename]}
            return list(sut.format_entry(trace))[0]["file"]

        self.assertEqual(os.path.join(current, "file.c"), test("file.c"))
        self.assertEqual(os.path.join(current, "file.c"), test("./file.c"))
        self.assertEqual(os.path.join(parent, "file.c"), test("../file.c"))
        self.assertEqual(
            os.path.join(current, "file.c"), test(os.path.join(current, "file.c"))
        )

    def test_sip(self):
        def create_status_report(filename, message):
            content = """#!/usr/bin/env sh
                         echo 'sa-la-la-la'
                         echo 'la-la-la'
                         echo '{0}'
                         echo 'sa-la-la-la'
                         echo 'la-la-la'
                      """.format(
                message
            )
            lines = [line.strip() for line in content.split("\n")]
            with open(filename, "w") as handle:
                handle.write("\n".join(lines))
                handle.close()
            os.chmod(filename, 0x1FF)

        def create_csrutil(dest_dir, status):
            filename = os.path.join(dest_dir, "csrutil")
            message = "System Integrity Protection status: {0}".format(status)
            return create_status_report(filename, message)

        def create_sestatus(dest_dir, status):
            filename = os.path.join(dest_dir, "sestatus")
            message = "SELinux status:\t{0}".format(status)
            return create_status_report(filename, message)

        ENABLED = "enabled"
        DISABLED = "disabled"

        OSX = "darwin"

        with libear.TemporaryDirectory() as tmpdir:
            saved = os.environ["PATH"]
            try:
                os.environ["PATH"] = tmpdir + ":" + saved

                create_csrutil(tmpdir, ENABLED)
                self.assertTrue(sut.is_preload_disabled(OSX))

                create_csrutil(tmpdir, DISABLED)
                self.assertFalse(sut.is_preload_disabled(OSX))
            finally:
                os.environ["PATH"] = saved

        saved = os.environ["PATH"]
        try:
            os.environ["PATH"] = ""
            # shall be false when it's not in the path
            self.assertFalse(sut.is_preload_disabled(OSX))

            self.assertFalse(sut.is_preload_disabled("unix"))
        finally:
            os.environ["PATH"] = saved
