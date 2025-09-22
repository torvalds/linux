#!/usr/bin/env python
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ==------------------------------------------------------------------------==#

import os
import glob
import re
import subprocess
import json
import datetime
import argparse

try:
    from urllib.parse import urlencode
    from urllib.request import urlopen, Request
except ImportError:
    from urllib import urlencode
    from urllib2 import urlopen, Request


parser = argparse.ArgumentParser()
parser.add_argument("benchmark_directory")
parser.add_argument("--runs", type=int, default=10)
parser.add_argument("--wrapper", default="")
parser.add_argument("--machine", required=True)
parser.add_argument("--revision", required=True)
parser.add_argument("--threads", action="store_true")
parser.add_argument(
    "--url",
    help="The lnt server url to send the results to",
    default="http://localhost:8000/db_default/v4/link/submitRun",
)
args = parser.parse_args()


class Bench:
    def __init__(self, directory, variant):
        self.directory = directory
        self.variant = variant

    def __str__(self):
        if not self.variant:
            return self.directory
        return "%s-%s" % (self.directory, self.variant)


def getBenchmarks():
    ret = []
    for i in glob.glob("*/response*.txt"):
        m = re.match("response-(.*)\.txt", os.path.basename(i))
        variant = m.groups()[0] if m else None
        ret.append(Bench(os.path.dirname(i), variant))
    return ret


def parsePerfNum(num):
    num = num.replace(b",", b"")
    try:
        return int(num)
    except ValueError:
        return float(num)


def parsePerfLine(line):
    ret = {}
    line = line.split(b"#")[0].strip()
    if len(line) != 0:
        p = line.split()
        ret[p[1].strip().decode("ascii")] = parsePerfNum(p[0])
    return ret


def parsePerf(output):
    ret = {}
    lines = [x.strip() for x in output.split(b"\n")]

    seconds = [x for x in lines if b"seconds time elapsed" in x][0]
    seconds = seconds.strip().split()[0].strip()
    ret["seconds-elapsed"] = parsePerfNum(seconds)

    measurement_lines = [x for x in lines if b"#" in x]
    for l in measurement_lines:
        ret.update(parsePerfLine(l))
    return ret


def run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        print(e.output)
        raise e


def combinePerfRun(acc, d):
    for k, v in d.items():
        a = acc.get(k, [])
        a.append(v)
        acc[k] = a


def perf(cmd):
    # Discard the first run to warm up any system cache.
    run(cmd)

    ret = {}
    wrapper_args = [x for x in args.wrapper.split(",") if x]
    for i in range(args.runs):
        os.unlink("t")
        out = run(wrapper_args + ["perf", "stat"] + cmd)
        r = parsePerf(out)
        combinePerfRun(ret, r)
    os.unlink("t")
    return ret


def runBench(bench):
    thread_arg = [] if args.threads else ["--no-threads"]
    os.chdir(bench.directory)
    suffix = "-%s" % bench.variant if bench.variant else ""
    response = "response" + suffix + ".txt"
    ret = perf(["../ld.lld", "@" + response, "-o", "t"] + thread_arg)
    ret["name"] = str(bench)
    os.chdir("..")
    return ret


def buildLntJson(benchmarks):
    start = datetime.datetime.utcnow().isoformat()
    tests = [runBench(b) for b in benchmarks]
    end = datetime.datetime.utcnow().isoformat()
    ret = {
        "format_version": 2,
        "machine": {"name": args.machine},
        "run": {
            "end_time": start,
            "start_time": end,
            "llvm_project_revision": args.revision,
        },
        "tests": tests,
    }
    return json.dumps(ret, sort_keys=True, indent=4)


def submitToServer(data):
    data2 = urlencode({"input_data": data}).encode("ascii")
    urlopen(Request(args.url, data2))


os.chdir(args.benchmark_directory)
data = buildLntJson(getBenchmarks())
submitToServer(data)
