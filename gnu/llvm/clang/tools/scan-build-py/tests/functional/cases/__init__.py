# -*- coding: utf-8 -*-
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import re
import os.path
import subprocess


def load_tests(loader, suite, pattern):
    from . import test_from_cdb

    suite.addTests(loader.loadTestsFromModule(test_from_cdb))
    from . import test_from_cmd

    suite.addTests(loader.loadTestsFromModule(test_from_cmd))
    from . import test_create_cdb

    suite.addTests(loader.loadTestsFromModule(test_create_cdb))
    from . import test_exec_anatomy

    suite.addTests(loader.loadTestsFromModule(test_exec_anatomy))
    return suite


def make_args(target):
    this_dir, _ = os.path.split(__file__)
    path = os.path.abspath(os.path.join(this_dir, "..", "src"))
    return [
        "make",
        "SRCDIR={}".format(path),
        "OBJDIR={}".format(target),
        "-f",
        os.path.join(path, "build", "Makefile"),
    ]


def silent_call(cmd, *args, **kwargs):
    kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    return subprocess.call(cmd, *args, **kwargs)


def silent_check_call(cmd, *args, **kwargs):
    kwargs.update({"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT})
    return subprocess.check_call(cmd, *args, **kwargs)


def call_and_report(analyzer_cmd, build_cmd):
    child = subprocess.Popen(
        analyzer_cmd + ["-v"] + build_cmd,
        universal_newlines=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    pattern = re.compile("Report directory created: (.+)")
    directory = None
    for line in child.stdout.readlines():
        match = pattern.search(line)
        if match and match.lastindex == 1:
            directory = match.group(1)
            break
    child.stdout.close()
    child.wait()

    return (child.returncode, directory)


def check_call_and_report(analyzer_cmd, build_cmd):
    exit_code, result = call_and_report(analyzer_cmd, build_cmd)
    if exit_code != 0:
        raise subprocess.CalledProcessError(exit_code, analyzer_cmd + build_cmd, None)
    else:
        return result


def create_empty_file(filename):
    with open(filename, "a") as handle:
        pass
