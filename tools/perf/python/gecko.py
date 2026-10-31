#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
gecko.py - Convert perf record output to Firefox's gecko profile format
"""
from __future__ import annotations

import argparse
import functools
import json
import os
import sys
import tempfile
import threading
import urllib.parse
import uuid
import webbrowser
from dataclasses import dataclass, field
from http.server import HTTPServer, SimpleHTTPRequestHandler
from typing import Dict, List, NamedTuple, Optional, Tuple

import perf


# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L156
class Frame(NamedTuple):
    """A single stack frame in the gecko profile format."""
    string_id: int
    relevantForJS: bool
    innerWindowID: int
    implementation: None
    optimizations: None
    line: None
    column: None
    category: int
    subcategory: Optional[int]


# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L216
class Stack(NamedTuple):
    """A single stack in the gecko profile format."""
    prefix_id: Optional[int]
    frame_id: int


# https://github.com/firefox-devtools/profiler/blob/53970305b51b9b472e26d7457fee1d66cd4e2737/src/types/gecko-profile.js#L90
class Sample(NamedTuple):
    """A single sample in the gecko profile format."""
    stack_id: Optional[int]
    time_ms: float
    responsiveness: int


@dataclass
class Tables:
    """Interned tables for the gecko profile format."""
    frame_table: List[Frame] = field(default_factory=list)
    string_table: List[str] = field(default_factory=list)
    string_map: Dict[str, int] = field(default_factory=dict)
    stack_table: List[Stack] = field(default_factory=list)
    stack_map: Dict[Tuple[Optional[int], int], int] = field(default_factory=dict)
    frame_map: Dict[str, int] = field(default_factory=dict)


@dataclass
class Thread:
    """A builder for a profile of the thread."""
    comm: str
    pid: int
    tid: int
    user_category: int
    kernel_category: int
    samples: List[Sample] = field(default_factory=list)
    tables: Tables = field(default_factory=Tables)

    def _intern_stack(self, frame_id: int, prefix_id: Optional[int]) -> int:
        """Gets a matching stack, or saves the new stack. Returns a Stack ID."""
        key = (prefix_id, frame_id)
        stack_id = self.tables.stack_map.get(key)
        if stack_id is None:
            stack_id = len(self.tables.stack_table)
            self.tables.stack_table.append(Stack(prefix_id=prefix_id, frame_id=frame_id))
            self.tables.stack_map[key] = stack_id
        return stack_id

    def _intern_string(self, string: str) -> int:
        """Gets a matching string, or saves the new string. Returns a String ID."""
        string_id = self.tables.string_map.get(string)
        if string_id is not None:
            return string_id
        string_id = len(self.tables.string_table)
        self.tables.string_table.append(string)
        self.tables.string_map[string] = string_id
        return string_id

    def _intern_frame(self, frame_str: str) -> int:
        """Gets a matching stack frame, or saves the new frame. Returns a Frame ID."""
        frame_id = self.tables.frame_map.get(frame_str)
        if frame_id is not None:
            return frame_id
        frame_id = len(self.tables.frame_table)
        self.tables.frame_map[frame_str] = frame_id
        string_id = self._intern_string(frame_str)

        category = self.user_category
        if (frame_str.find('kallsyms') != -1 or
                frame_str.find('/vmlinux') != -1 or
                frame_str.endswith('.ko)')):
            category = self.kernel_category

        self.tables.frame_table.append(Frame(
            string_id=string_id,
            relevantForJS=False,
            innerWindowID=0,
            implementation=None,
            optimizations=None,
            line=None,
            column=None,
            category=category,
            subcategory=None,
        ))
        return frame_id

    def add_sample(self, comm: str, stack: List[str], time_ms: float) -> None:
        """Add a timestamped stack trace sample to the thread builder."""
        if self.comm != comm:
            self.comm = comm

        prefix_stack_id: Optional[int] = None
        for frame in stack:
            frame_id = self._intern_frame(frame)
            prefix_stack_id = self._intern_stack(frame_id, prefix_stack_id)

        if prefix_stack_id is not None:
            self.samples.append(Sample(stack_id=prefix_stack_id,
                                       time_ms=time_ms,
                                       responsiveness=0))

    def to_json_dict(self) -> Dict:
        """Converts current Thread to GeckoThread JSON format."""
        return {
            "tid": self.tid,
            "pid": self.pid,
            "name": self.comm,
            "markers": {
                "schema": {
                    "name": 0,
                    "startTime": 1,
                    "endTime": 2,
                    "phase": 3,
                    "category": 4,
                    "data": 5,
                },
                "data": [],
            },
            "samples": {
                "schema": {
                    "stack": 0,
                    "time": 1,
                    "responsiveness": 2,
                },
                "data": self.samples
            },
            "frameTable": {
                "schema": {
                    "location": 0,
                    "relevantForJS": 1,
                    "innerWindowID": 2,
                    "implementation": 3,
                    "optimizations": 4,
                    "line": 5,
                    "column": 6,
                    "category": 7,
                    "subcategory": 8,
                },
                "data": self.tables.frame_table,
            },
            "stackTable": {
                "schema": {
                    "prefix": 0,
                    "frame": 1,
                },
                "data": self.tables.stack_table,
            },
            "stringTable": self.tables.string_table,
            "registerTime": 0,
            "unregisterTime": None,
            "processType": "default",
        }


class CORSRequestHandler(SimpleHTTPRequestHandler):
    """Enable CORS for requests from profiler.firefox.com."""
    def __init__(self, *args, allowed_file: Optional[str] = None, **kwargs):
        self.allowed_file = allowed_file
        super().__init__(*args, **kwargs)

    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', 'https://profiler.firefox.com')
        super().end_headers()

    def do_GET(self):
        if self.allowed_file and self.path.split('?')[0] != f'/{self.allowed_file}':
            self.send_error(403, "Access denied")
            return
        super().do_GET()

    def list_directory(self, path):
        self.send_error(403, "Directory listing forbidden")
        return None


@dataclass
class CategoryData:
    """Category configuration for the gecko profile."""
    user_index: int = 0
    kernel_index: int = 1
    categories: List[Dict] = field(default_factory=list)


class GeckoCLI:
    """Command-line interface for converting perf data to Gecko format."""
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.tid_to_thread: Dict[int, Thread] = {}
        self.start_time_ms: Optional[float] = None
        self.session: Optional[perf.session] = None
        self.product = args.product
        self.cat_data = CategoryData(
            categories=[
                {
                    "name": 'User',
                    "color": args.user_color,
                    "subcategories": ['Other']
                },
                {
                    "name": 'Kernel',
                    "color": args.kernel_color,
                    "subcategories": ['Other']
                },
            ]
        )

    def process_event(self, sample) -> None:
        """Process a single perf sample event."""
        if self.args.event_name and self.args.event_name not in str(sample.evsel):
            return

        # sample_time is in nanoseconds. Gecko wants milliseconds.
        time_ms = sample.sample_time / 1000000.0
        pid = sample.sample_pid
        tid = sample.sample_tid

        if self.start_time_ms is None:
            self.start_time_ms = time_ms

        try:
            thread_info = self.session.find_thread(sample.sample_pid, sample.sample_tid) if self.session else None
            comm = (thread_info.comm() if thread_info is not None else None) or "[unknown]"
        except AttributeError:
            comm = "[unknown]"

        stack = []
        callchain = sample.callchain
        if callchain:
            for entry in callchain:
                symbol = entry.symbol or "[unknown]"
                dso = entry.dso or "[unknown]"
                stack.append(f"{symbol} (in {dso})")
            # Reverse because Gecko wants root first.
            stack.reverse()
        else:
            symbol = sample.symbol or "[unknown]"
            dso = sample.dso or "[unknown]"
            stack.append(f"{symbol} (in {dso})")

        thread = self.tid_to_thread.get(tid)
        if thread is None:
            thread = Thread(comm=comm, pid=pid, tid=tid,
                            user_category=self.cat_data.user_index,
                            kernel_category=self.cat_data.kernel_index)
            self.tid_to_thread[tid] = thread
        thread.add_sample(comm=comm, stack=stack, time_ms=time_ms)

    def run(self) -> None:
        """Run the conversion process."""
        input_file = self.args.input or "perf.data"
        if input_file != "-" and not os.path.exists(input_file):
            print(f"Error: {input_file} not found.", file=sys.stderr)
            sys.exit(1)

        try:
            self.session = perf.session(perf.data(input_file), sample=self.process_event)
        except (OSError, RuntimeError, ValueError) as e:
            print(f"Error opening session: {e}", file=sys.stderr)
            sys.exit(1)

        if self.session:
            try:
                self.session.process_events()
            finally:
                # Break the reference cycle between perf.session and self.process_event
                # because perf.session lacks cyclic GC support (tp_traverse).
                self.session = None

        threads = [t.to_json_dict() for t in self.tid_to_thread.values()]

        gecko_profile = {
            "meta": {
                "interval": 1,
                "processType": 0,
                "product": self.product,
                "stackwalk": 1,
                "debug": 0,
                "gcpoison": 0,
                "asyncstack": 1,
                "startTime": self.start_time_ms or 0,
                "shutdownTime": None,
                "version": 24,
                "presymbolicated": True,
                "categories": self.cat_data.categories,
                "markerSchema": [],
            },
            "libs": [],
            "threads": threads,
            "processes": [],
            "pausedRanges": [],
        }

        output_file = self.args.save_only
        if output_file is None:
            self._write_and_launch(gecko_profile)
        else:
            print(f'[ perf gecko: Captured and wrote into {output_file} ]')
            with open(output_file, 'w', encoding='utf-8') as f:
                json.dump(gecko_profile, f, indent=2)

    def _write_and_launch(self, profile: Dict) -> None:
        """Write the profile to a file and launch the Firefox profiler."""
        print("Starting Firefox Profiler on your default browser...")

        with tempfile.TemporaryDirectory() as tmp_dir_name:
            profile_name = f'gecko_profile_{uuid.uuid4().hex}.json'
            filename = os.path.join(tmp_dir_name, profile_name)

            with open(filename, 'w', encoding='utf-8') as f:
                json.dump(profile, f, indent=2)

            handler = functools.partial(
                CORSRequestHandler, directory=tmp_dir_name, allowed_file=profile_name
            )
            try:
                httpd = HTTPServer(('127.0.0.1', 0), handler)
            except OSError as e:
                print(f"Error starting HTTP server: {e}", file=sys.stderr)
                sys.exit(1)

            port = httpd.server_port

            def start_server():
                httpd.serve_forever()

            thread = threading.Thread(target=start_server, daemon=True)
            thread.start()

            safe_string = urllib.parse.quote_plus(f'http://127.0.0.1:{port}/{profile_name}')
            url = f'https://profiler.firefox.com/from-url/{safe_string}'
            print(f"Please open the following URL in your browser to view the profile:\n\n{url}\n")
            launch_html = os.path.join(tmp_dir_name, 'launch.html')
            with open(launch_html, 'w', encoding='utf-8') as f:
                f.write(f'<!DOCTYPE html><html><head>'
                        f'<meta http-equiv="refresh" content="0;url={url}">'
                        f'<script>window.location.replace({json.dumps(url)});</script>'
                        f'</head><body>Redirecting to Firefox Profiler...</body></html>')
            if not webbrowser.open(f'file://{launch_html}'):
                print("Failed to open browser, please open the URL manually.")

            print(f'[ perf gecko: Captured and wrote into {filename} ]')
            print("Press Ctrl+C to stop the local server.")
            try:
                # Keep the main thread alive so the daemon thread can serve requests
                stop_event = threading.Event()
                while True:
                    stop_event.wait(1)
            except KeyboardInterrupt:
                print("\nStopping server...")
                httpd.shutdown()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert perf.data to Firefox's Gecko Profile format"
    )
    parser.add_argument('--product', default='perf', help='Product name (e.g. perf)')
    parser.add_argument('--user-color', default='yellow',
                        help='Color for the User category',
                        choices=['yellow', 'blue', 'purple', 'green', 'orange', 'red',
                                 'grey', 'magenta'])
    parser.add_argument('--kernel-color', default='orange',
                        help='Color for the Kernel category',
                        choices=['yellow', 'blue', 'purple', 'green', 'orange', 'red',
                                 'grey', 'magenta'])
    parser.add_argument('--save-only',
                        help='Save the output to a file instead of opening Firefox\'s profiler')
    parser.add_argument("-i", "--input", help="input perf.data file")
    parser.add_argument("-e", "--event", default="", dest="event_name", type=str,
                        help="specify the event to generate gecko profile for")

    cli_args = parser.parse_args()
    cli = GeckoCLI(cli_args)
    cli.run()
