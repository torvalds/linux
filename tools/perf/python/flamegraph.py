#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
flamegraph.py - create flame graphs from perf samples using perf python module
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request
from typing import Dict, Optional, Union
import perf

MINIMAL_HTML = """<head>
  <link rel="stylesheet" type="text/css" href="https://cdn.jsdelivr.net/npm/d3-flame-graph@4.1.3/dist/d3-flamegraph.css" integrity="sha256-fXXWjLZigZo3QBJIhjEA3Z2zUfS+vzMQhF2Rlvurfio=" crossorigin="anonymous">
</head>
<body>
  <div id="chart"></div>
  <script type="text/javascript" src="https://d3js.org/d3.v7.js" integrity="sha256-DA1YviEGwU4kL8dbwJoXRngAYwdRWAZl9ulVKgwGTqI=" crossorigin="anonymous"></script>
  <script type="text/javascript" src="https://cdn.jsdelivr.net/npm/d3-flame-graph@4.1.3/dist/d3-flamegraph.min.js" integrity="sha256-GEbx5jbtG9LDL8h9HiTWOv9+Txhim0u9gZNR8J6iI1M=" crossorigin="anonymous"></script>
  <script type="text/javascript">
  const stacks = [/** @flamegraph_json **/];
  // Note, options is unused.
  const options = [/** @options_json **/];

  var chart = flamegraph();
  d3.select("#chart")
        .datum(stacks[0])
        .call(chart);
  </script>
</body>
"""

class Node:
    """A node in the flame graph tree."""
    def __init__(self, name: str, libtype: str):
        self.name = name
        self.libtype = libtype
        self.value: int = 0
        self.children: dict[str, Node] = {}

    def to_json(self) -> Dict[str, Union[str, int, list[Dict]]]:
        """Convert the node to a JSON-serializable dictionary."""
        return {
            "n": self.name,
            "l": self.libtype,
            "v": self.value,
            "c": [x.to_json() for x in self.children.values()]
        }


class FlameGraphCLI:
    """Command-line interface for generating flame graphs."""
    def __init__(self, args):
        self.args = args
        self.stack = Node("all", "root")
        self.session = None

    @staticmethod
    def get_libtype_from_dso(dso: Optional[str]) -> str:
        """Determine the library type from the DSO name."""
        if dso and (dso == "[kernel.kallsyms]" or dso.endswith("/vmlinux") or dso == "[kernel]"):
            return "kernel"
        return ""

    @staticmethod
    def find_or_create_node(node: Node, name: str, libtype: str) -> Node:
        """Find a child node with the given name or create a new one."""
        if name in node.children:
            return node.children[name]
        child = Node(name, libtype)
        node.children[name] = child
        return child

    def process_event(self, sample) -> None:
        """Process a single perf sample event."""
        if self.args.event_name and self.args.event_name not in str(sample.evsel):
            return

        pid = sample.sample_pid
        dso_type = ""
        try:
            thread = self.session.find_thread(sample.sample_pid, sample.sample_tid)
            comm = (thread.comm() if thread else None) or "[unknown]"
        except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError):
            comm = "[unknown]"

        if pid == 0:
            comm = comm if comm != "[unknown]" else "swapper"
            dso_type = "kernel"
        else:
            comm = f"{comm} ({pid})"

        node = self.find_or_create_node(self.stack, comm, dso_type)

        callchain = sample.callchain
        if callchain:
            # We want to traverse from root to leaf.
            # perf callchain iterator gives leaf to root.
            # We collect them and reverse.
            frames = list(callchain)
            for entry in reversed(frames):
                name = entry.symbol or "[unknown]"
                libtype = self.get_libtype_from_dso(entry.dso)
                node = self.find_or_create_node(node, name, libtype)
        else:
            # Fallback if no callchain
            name = (sample.symbol or '[unknown]')
            libtype = self.get_libtype_from_dso((sample.dso or '[unknown]'))
            node = self.find_or_create_node(node, name, libtype)

        node.value += 1

    def get_report_header(self) -> str:
        """Get the header from the perf report."""
        try:
            input_file = self.args.input or "perf.data"
            if input_file == "-":
                return ""
            mode = os.stat(input_file).st_mode
            import stat
            if stat.S_ISFIFO(mode) or stat.S_ISCHR(mode):
                return ""
            output = subprocess.check_output(["perf", "report", "--header-only", "-i", input_file])
            result = output.decode("utf-8")
            if self.args.event_name:
                result += "\nFocused event: " + self.args.event_name
            return result
        except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError,
                subprocess.CalledProcessError):
            return ""

    def run(self) -> None:
        """Run the flame graph generation."""
        input_file = self.args.input or "perf.data"
        if input_file != "-" and not os.path.exists(input_file):
            print(f"Error: {input_file} not found. (try 'perf record' first)", file=sys.stderr)
            sys.exit(1)

        try:
            self.session = perf.session(perf.data(input_file),
                                        sample=self.process_event)
        except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError) as e:
            print(f"Error opening session: {e}", file=sys.stderr)
            sys.exit(1)

        try:
            self.session.process_events()
        finally:
            # perf.session is a C extension type that holds a strong reference to the
            # bound method self.process_event (which references self) and does not
            # implement cyclic GC (tp_traverse). Explicitly clear self.session to break
            # the reference cycle and free the underlying session resources deterministically.
            self.session = None

        stacks_json = json.dumps(self.stack, default=lambda x: x.to_json())
        # Escape HTML special characters to prevent XSS
        stacks_json = stacks_json.replace("<", "\\u003c") \
            .replace(">", "\\u003e").replace("&", "\\u0026")

        if self.args.format == "html":
            report_header = self.get_report_header()
            options = {
                "colorscheme": self.args.colorscheme,
                "context": report_header
            }
            options_json = json.dumps(options)
            options_json = options_json.replace("<", "\\u003c") \
                .replace(">", "\\u003e").replace("&", "\\u0026")

            template = self.args.template
            template_sha256sum = None
            output_str = None

            if not os.path.isfile(template):
                if template.startswith("http://") or template.startswith("https://"):
                    if not self.args.allow_download:
                        print("Warning: Downloading templates is disabled. "
                              "Use --allow-download.", file=sys.stderr)
                        template = None
                else:
                    print(f"Warning: Template file '{template}' not found.", file=sys.stderr)
                    if self.args.allow_download:
                        print("Using default CDN template.", file=sys.stderr)
                        template = (
                            "https://cdn.jsdelivr.net/npm/d3-flame-graph@4.1.3/dist/templates/"
                            "d3-flamegraph-base.html"
                        )
                        template_sha256sum = (
                            "f6a4aa7edffda4fb9bd71eb0eb75bc44d0bb34cd9efbd053f6095bc5c28d702b"
                        )
                    else:
                        template = None

            use_minimal = False
            try:
                if not template:
                    use_minimal = True
                elif template.startswith(("http://", "https://")):
                    with urllib.request.urlopen(template) as url_template:
                        output_str = "".join([l.decode("utf-8") for l in url_template.readlines()])
                else:
                    with open(template, "r", encoding="utf-8") as f:
                        output_str = f.read()
            except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError) as err:
                print(f"Error reading template {template}: {err}\n", file=sys.stderr)
                use_minimal = True

            if use_minimal:
                print("Using internal minimal HTML that refers to d3's web site. JavaScript " +
                      "loaded this way from a local file may be blocked unless your " +
                      "browser has relaxed permissions. Run with '--allow-download' to fetch " +
                      "the full D3 HTML template.", file=sys.stderr)
                output_str = MINIMAL_HTML

            elif template_sha256sum:
                assert output_str is not None
                download_sha256sum = hashlib.sha256(
                    output_str.encode("utf-8")
                ).hexdigest()
                if download_sha256sum != template_sha256sum:
                    s = None
                    while s not in ["y", "n"]:
                        try:
                            s = input(f"""Unexpected template sha256sum.
{download_sha256sum} != {template_sha256sum}, for:
{template}
continue?[yn] """).lower()
                        except EOFError:
                            s = "n"
                    if s == "n":
                        sys.exit(1)

            assert output_str is not None
            replacements = {
                "/** @options_json **/": options_json,
                "/** @flamegraph_json **/": stacks_json,
            }
            # Use a lambda replacement so re.sub does not interpret backslashes
            # in the JSON strings (such as \n or \u003c) as regex escape sequences.
            output_str = re.sub(
                r"/\*\* @(?:options_json|flamegraph_json) \*\*/",
                lambda m: replacements[m.group(0)],
                output_str,
            )
            output_fn = self.args.output or "flamegraph.html"
        else:
            output_str = stacks_json
            output_fn = self.args.output or "stacks.json"

        if output_fn == "-":
            with open(sys.stdout.fileno(), "w", encoding="utf-8", closefd=False) as out:
                out.write(output_str)
        else:
            print(f"dumping data to {output_fn}")
            with open(output_fn, "w", encoding="utf-8") as out:
                out.write(output_str)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create flame graphs using perf python module.")
    parser.add_argument("-f", "--format", default="html", choices=["json", "html"],
                        help="output file format")
    parser.add_argument("-o", "--output", help="output file name")
    parser.add_argument("--template",
                        default="/usr/share/d3-flame-graph/d3-flamegraph-base.html",
                        help="path to flame graph HTML template")
    parser.add_argument("--colorscheme", default="blue-green",
                        help="flame graph color scheme", choices=["blue-green", "orange"])
    parser.add_argument("-i", "--input", help="input perf.data file")
    parser.add_argument("--allow-download", default=False, action="store_true",
                        help="allow unprompted downloading of HTML template")
    parser.add_argument("-e", "--event", default="", dest="event_name", type=str,
                        help="specify the event to generate flamegraph for")

    cli_args = parser.parse_args()
    cli = FlameGraphCLI(cli_args)
    cli.run()
