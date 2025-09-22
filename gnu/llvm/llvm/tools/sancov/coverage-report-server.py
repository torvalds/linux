#!/usr/bin/env python3
# ===- symcov-report-server.py - Coverage Reports HTTP Serve --*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#
"""(EXPERIMENTAL) HTTP server to browse coverage reports from .symcov files.

Coverage reports for big binaries are too huge, generating them statically
makes no sense. Start the server and go to localhost:8001 instead.

Usage:
    ./tools/sancov/symcov-report-server.py \
            --symcov coverage_data.symcov \
            --srcpath root_src_dir

Other options:
    --port port_number - specifies the port to use (8001)
    --host host_name - host name to bind server to (127.0.0.1)
"""

from __future__ import print_function

import argparse
import http.server
import json
import socketserver
import time
import html
import os
import string
import math
import urllib

INDEX_PAGE_TMPL = """
<html>
<head>
  <title>Coverage Report</title>
  <style>
    .lz { color: lightgray; }
  </style>
</head>
<body>
    <table>
      <tr><th>File</th><th>Coverage</th></tr>
      <tr><td><em>Files with 0 coverage are not shown.</em></td></tr>
$filenames
    </table>
</body>
</html>
"""

CONTENT_PAGE_TMPL = """
<html>
<head>
  <title>$path</title>
  <style>
    .covered { background: lightgreen; }
    .not-covered { background: lightcoral; }
    .partially-covered { background: navajowhite; }
    .lz { color: lightgray; }
  </style>
</head>
<body>
<pre>
$content
</pre>
</body>
</html>
"""

FILE_URI_PREFIX = "/file/"


class SymcovData:
    def __init__(self, symcov_json):
        self.covered_points = frozenset(symcov_json["covered-points"])
        self.point_symbol_info = symcov_json["point-symbol-info"]
        self.file_coverage = self.compute_filecoverage()

    def filenames(self):
        return self.point_symbol_info.keys()

    def has_file(self, filename):
        return filename in self.point_symbol_info

    def compute_linemap(self, filename):
        """Build a line_number->css_class map."""
        points = self.point_symbol_info.get(filename, dict())

        line_to_points = dict()
        for fn, points in points.items():
            for point, loc in points.items():
                line = int(loc.split(":")[0])
                line_to_points.setdefault(line, []).append(point)

        result = dict()
        for line, points in line_to_points.items():
            status = "covered"
            covered_points = self.covered_points & set(points)
            if not len(covered_points):
                status = "not-covered"
            elif len(covered_points) != len(points):
                status = "partially-covered"
            result[line] = status
        return result

    def compute_filecoverage(self):
        """Build a filename->pct coverage."""
        result = dict()
        for filename, fns in self.point_symbol_info.items():
            file_points = []
            for fn, points in fns.items():
                file_points.extend(points.keys())
            covered_points = self.covered_points & set(file_points)
            result[filename] = int(
                math.ceil(len(covered_points) * 100 / len(file_points))
            )
        return result


def format_pct(pct):
    pct_str = str(max(0, min(100, pct)))
    zeroes = "0" * (3 - len(pct_str))
    if zeroes:
        zeroes = '<span class="lz">{0}</span>'.format(zeroes)
    return zeroes + pct_str


class ServerHandler(http.server.BaseHTTPRequestHandler):
    symcov_data = None
    src_path = None

    def do_GET(self):
        norm_path = os.path.normpath(
            urllib.parse.unquote(self.path[len(FILE_URI_PREFIX) :])
        )
        if self.path == "/":
            self.send_response(200)
            self.send_header("Content-type", "text/html; charset=utf-8")
            self.end_headers()

            filelist = []
            for filename in sorted(self.symcov_data.filenames()):
                file_coverage = self.symcov_data.file_coverage[filename]
                if not file_coverage:
                    continue
                filelist.append(
                    '<tr><td><a href="{prefix}{name}">{name}</a></td>'
                    "<td>{coverage}%</td></tr>".format(
                        prefix=FILE_URI_PREFIX,
                        name=html.escape(filename, quote=True),
                        coverage=format_pct(file_coverage),
                    )
                )

            response = string.Template(INDEX_PAGE_TMPL).safe_substitute(
                filenames="\n".join(filelist)
            )
            self.wfile.write(response.encode("UTF-8", "replace"))
        elif self.symcov_data.has_file(norm_path):
            filename = norm_path
            filepath = os.path.join(self.src_path, filename)
            if not os.path.exists(filepath):
                self.send_response(404)
                self.end_headers()
                return

            self.send_response(200)
            self.send_header("Content-type", "text/html; charset=utf-8")
            self.end_headers()

            linemap = self.symcov_data.compute_linemap(filename)

            with open(filepath, "r", encoding="utf8") as f:
                content = "\n".join(
                    [
                        "<span class='{cls}'>{line}&nbsp;</span>".format(
                            line=html.escape(line.rstrip()),
                            cls=linemap.get(line_no, ""),
                        )
                        for line_no, line in enumerate(f, start=1)
                    ]
                )

            response = string.Template(CONTENT_PAGE_TMPL).safe_substitute(
                path=self.path[1:], content=content
            )

            self.wfile.write(response.encode("UTF-8", "replace"))
        else:
            self.send_response(404)
            self.end_headers()


def main():
    parser = argparse.ArgumentParser(description="symcov report http server.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=8001)
    parser.add_argument("--symcov", required=True, type=argparse.FileType("r"))
    parser.add_argument("--srcpath", required=True)
    args = parser.parse_args()

    print("Loading coverage...")
    symcov_json = json.load(args.symcov)
    ServerHandler.symcov_data = SymcovData(symcov_json)
    ServerHandler.src_path = args.srcpath

    socketserver.TCPServer.allow_reuse_address = True
    httpd = socketserver.TCPServer((args.host, args.port), ServerHandler)
    print("Serving at {host}:{port}".format(host=args.host, port=args.port))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    httpd.server_close()


if __name__ == "__main__":
    main()
