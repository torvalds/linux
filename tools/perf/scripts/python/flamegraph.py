# flamegraph.py - create flame graphs from perf samples
# SPDX-License-Identifier: GPL-2.0
#
# Usage:
#
#     perf record -a -g -F 99 sleep 60
#     perf script report flamegraph
#
# Combined:
#
#     perf script flamegraph -a -F 99 sleep 60
#
# Written by Andreas Gerstmayr <agerstmayr@redhat.com>
# Flame Graphs invented by Brendan Gregg <bgregg@netflix.com>
# Works in tandem with d3-flame-graph by Martin Spier <mspier@netflix.com>

from __future__ import print_function
import sys
import os
import argparse
import json


class Node:
    def __init__(self, name, libtype=""):
        self.name = name
        self.libtype = libtype
        self.value = 0
        self.children = []

    def toJSON(self):
        return {
            "n": self.name,
            "l": self.libtype,
            "v": self.value,
            "c": self.children
        }


class FlameGraphCLI:
    def __init__(self, args):
        self.args = args
        self.stack = Node("root")

        if self.args.format == "html" and \
                not os.path.isfile(self.args.template):
            print("Flame Graph template {} does not exist. Please install "
                  "the js-d3-flame-graph (RPM) or libjs-d3-flame-graph (deb) "
                  "package, specify an existing flame graph template "
                  "(--template PATH) or another output format "
                  "(--format FORMAT).".format(self.args.template),
                  file=sys.stderr)
            sys.exit(1)

    def find_or_create_node(self, node, name, dso):
        libtype = "kernel" if dso == "[kernel.kallsyms]" else ""
        if name is None:
            name = "[unknown]"

        for child in node.children:
            if child.name == name and child.libtype == libtype:
                return child

        child = Node(name, libtype)
        node.children.append(child)
        return child

    def process_event(self, event):
        node = self.find_or_create_node(self.stack, event["comm"], None)
        if "callchain" in event:
            for entry in reversed(event['callchain']):
                node = self.find_or_create_node(
                    node, entry.get("sym", {}).get("name"), event.get("dso"))
        else:
            node = self.find_or_create_node(
                node, entry.get("symbol"), event.get("dso"))
        node.value += 1

    def trace_end(self):
        json_str = json.dumps(self.stack, default=lambda x: x.toJSON())

        if self.args.format == "html":
            try:
                with open(self.args.template) as f:
                    output_str = f.read().replace("/** @flamegraph_json **/",
                                                  json_str)
            except IOError as e:
                print("Error reading template file: {}".format(e), file=sys.stderr)
                sys.exit(1)
            output_fn = self.args.output or "flamegraph.html"
        else:
            output_str = json_str
            output_fn = self.args.output or "stacks.json"

        if output_fn == "-":
            sys.stdout.write(output_str)
        else:
            print("dumping data to {}".format(output_fn))
            try:
                with open(output_fn, "w") as out:
                    out.write(output_str)
            except IOError as e:
                print("Error writing output file: {}".format(e), file=sys.stderr)
                sys.exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create flame graphs.")
    parser.add_argument("-f", "--format",
                        default="html", choices=["json", "html"],
                        help="output file format")
    parser.add_argument("-o", "--output",
                        help="output file name")
    parser.add_argument("--template",
                        default="/usr/share/d3-flame-graph/d3-flamegraph-base.html",
                        help="path to flamegraph HTML template")
    parser.add_argument("-i", "--input",
                        help=argparse.SUPPRESS)

    args = parser.parse_args()
    cli = FlameGraphCLI(args)

    process_event = cli.process_event
    trace_end = cli.trace_end
