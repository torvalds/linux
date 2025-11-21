#!/usr/bin/env python3
# pylint: disable=R0902,R0911,R0912,R0914,R0915
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab@kernel.org>.
# SPDX-License-Identifier: GPL-2.0


"""
Library to parse the Linux Feature files and produce a ReST book.
"""

import os
import re
import sys

from glob import iglob


class ParseFeature:
    """
    Parses Documentation/features, allowing to generate ReST documentation
    from it.
    """

    h_name = "Feature"
    h_kconfig = "Kconfig"
    h_description = "Description"
    h_subsys = "Subsystem"
    h_status = "Status"
    h_arch = "Architecture"

    # Sort order for status. Others will be mapped at the end.
    status_map = {
        "ok":   0,
        "TODO": 1,
        "N/A":  2,
        # The only missing status is "..", which was mapped as "---",
        # as this is an special ReST cell value. Let it get the
        # default order (99).
    }

    def __init__(self, prefix, debug=0, enable_fname=False):
        """
        Sets internal variables
        """

        self.prefix = prefix
        self.debug = debug
        self.enable_fname = enable_fname

        self.data = {}

        # Initial maximum values use just the headers
        self.max_size_name = len(self.h_name)
        self.max_size_kconfig = len(self.h_kconfig)
        self.max_size_description = len(self.h_description)
        self.max_size_desc_word = 0
        self.max_size_subsys = len(self.h_subsys)
        self.max_size_status = len(self.h_status)
        self.max_size_arch = len(self.h_arch)
        self.max_size_arch_with_header = self.max_size_arch + self.max_size_arch
        self.description_size = 1

        self.msg = ""

    def emit(self, msg="", end="\n"):
        self.msg += msg + end

    def parse_error(self, fname, ln, msg, data=None):
        """
        Displays an error message, printing file name and line
        """

        if ln:
            fname += f"#{ln}"

        print(f"Warning: file {fname}: {msg}", file=sys.stderr, end="")

        if data:
            data = data.rstrip()
            print(f":\n\t{data}", file=sys.stderr)
        else:
            print("", file=sys.stderr)

    def parse_feat_file(self, fname):
        """Parses a single arch-support.txt feature file"""

        if os.path.isdir(fname):
            return

        base = os.path.basename(fname)

        if base != "arch-support.txt":
            if self.debug:
                print(f"ignoring {fname}", file=sys.stderr)
            return

        subsys = os.path.dirname(fname).split("/")[-2]
        self.max_size_subsys = max(self.max_size_subsys, len(subsys))

        feature_name = ""
        kconfig = ""
        description = ""
        comments = ""
        arch_table = {}

        if self.debug > 1:
            print(f"Opening {fname}", file=sys.stderr)

        if self.enable_fname:
            full_fname = os.path.abspath(fname)
            self.emit(f".. FILE {full_fname}")

        with open(fname, encoding="utf-8") as f:
            for ln, line in enumerate(f, start=1):
                line = line.strip()

                match = re.match(r"^\#\s+Feature\s+name:\s*(.*\S)", line)
                if match:
                    feature_name = match.group(1)

                    self.max_size_name = max(self.max_size_name,
                                             len(feature_name))
                    continue

                match = re.match(r"^\#\s+Kconfig:\s*(.*\S)", line)
                if match:
                    kconfig = match.group(1)

                    self.max_size_kconfig = max(self.max_size_kconfig,
                                                len(kconfig))
                    continue

                match = re.match(r"^\#\s+description:\s*(.*\S)", line)
                if match:
                    description = match.group(1)

                    self.max_size_description = max(self.max_size_description,
                                                    len(description))

                    words = re.split(r"\s+", line)[1:]
                    for word in words:
                        self.max_size_desc_word = max(self.max_size_desc_word,
                                                        len(word))

                    continue

                if re.search(r"^\\s*$", line):
                    continue

                if re.match(r"^\s*\-+\s*$", line):
                    continue

                if re.search(r"^\s*\|\s*arch\s*\|\s*status\s*\|\s*$", line):
                    continue

                match = re.match(r"^\#\s*(.*)$", line)
                if match:
                    comments += match.group(1)
                    continue

                match = re.match(r"^\s*\|\s*(\S+):\s*\|\s*(\S+)\s*\|\s*$", line)
                if match:
                    arch = match.group(1)
                    status = match.group(2)

                    self.max_size_status = max(self.max_size_status,
                                               len(status))
                    self.max_size_arch = max(self.max_size_arch, len(arch))

                    if status == "..":
                        status = "---"

                    arch_table[arch] = status

                    continue

                self.parse_error(fname, ln, "Line is invalid", line)

        if not feature_name:
            self.parse_error(fname, 0, "Feature name not found")
            return
        if not subsys:
            self.parse_error(fname, 0, "Subsystem not found")
            return
        if not kconfig:
            self.parse_error(fname, 0, "Kconfig not found")
            return
        if not description:
            self.parse_error(fname, 0, "Description not found")
            return
        if not arch_table:
            self.parse_error(fname, 0, "Architecture table not found")
            return

        self.data[feature_name] = {
            "where": fname,
            "subsys": subsys,
            "kconfig": kconfig,
            "description": description,
            "comments": comments,
            "table": arch_table,
        }

        self.max_size_arch_with_header = self.max_size_arch + len(self.h_arch)

    def parse(self):
        """Parses all arch-support.txt feature files inside self.prefix"""

        path = os.path.expanduser(self.prefix)

        if self.debug > 2:
            print(f"Running parser for {path}")

        example_path = os.path.join(path, "arch-support.txt")

        for fname in iglob(os.path.join(path, "**"), recursive=True):
            if fname != example_path:
                self.parse_feat_file(fname)

        return self.data

    def output_arch_table(self, arch, feat=None):
        """
        Output feature(s) for a given architecture.
        """

        title = f"Feature status on {arch} architecture"

        self.emit("=" * len(title))
        self.emit(title)
        self.emit("=" * len(title))
        self.emit()

        self.emit("=" * self.max_size_subsys + "  ", end="")
        self.emit("=" * self.max_size_name + "  ", end="")
        self.emit("=" * self.max_size_kconfig + "  ", end="")
        self.emit("=" * self.max_size_status + "  ", end="")
        self.emit("=" * self.max_size_description)

        self.emit(f"{self.h_subsys:<{self.max_size_subsys}}  ", end="")
        self.emit(f"{self.h_name:<{self.max_size_name}}  ", end="")
        self.emit(f"{self.h_kconfig:<{self.max_size_kconfig}}  ", end="")
        self.emit(f"{self.h_status:<{self.max_size_status}}  ", end="")
        self.emit(f"{self.h_description:<{self.max_size_description}}")

        self.emit("=" * self.max_size_subsys + "  ", end="")
        self.emit("=" * self.max_size_name + "  ", end="")
        self.emit("=" * self.max_size_kconfig + "  ", end="")
        self.emit("=" * self.max_size_status + "  ", end="")
        self.emit("=" * self.max_size_description)

        sorted_features = sorted(self.data.keys(),
                                 key=lambda x: (self.data[x]["subsys"],
                                                x.lower()))

        for name in sorted_features:
            if feat and name != feat:
                continue

            arch_table = self.data[name]["table"]

            if not arch in arch_table:
                continue

            self.emit(f"{self.data[name]['subsys']:<{self.max_size_subsys}}  ",
                  end="")
            self.emit(f"{name:<{self.max_size_name}}  ", end="")
            self.emit(f"{self.data[name]['kconfig']:<{self.max_size_kconfig}}  ",
                  end="")
            self.emit(f"{arch_table[arch]:<{self.max_size_status}}  ",
                  end="")
            self.emit(f"{self.data[name]['description']}")

        self.emit("=" * self.max_size_subsys + "  ", end="")
        self.emit("=" * self.max_size_name + "  ", end="")
        self.emit("=" * self.max_size_kconfig + "  ", end="")
        self.emit("=" * self.max_size_status + "  ", end="")
        self.emit("=" * self.max_size_description)

        return self.msg

    def output_feature(self, feat):
        """
        Output a feature on all architectures
        """

        title = f"Feature {feat}"

        self.emit("=" * len(title))
        self.emit(title)
        self.emit("=" * len(title))
        self.emit()

        if not feat in self.data:
            return

        if self.data[feat]["subsys"]:
            self.emit(f":Subsystem: {self.data[feat]['subsys']}")
        if self.data[feat]["kconfig"]:
            self.emit(f":Kconfig: {self.data[feat]['kconfig']}")

        desc = self.data[feat]["description"]
        desc = desc[0].upper() + desc[1:]
        desc = desc.rstrip(". \t")
        self.emit(f"\n{desc}.\n")

        com = self.data[feat]["comments"].strip()
        if com:
            self.emit("Comments")
            self.emit("--------")
            self.emit(f"\n{com}\n")

        self.emit("=" * self.max_size_arch + "  ", end="")
        self.emit("=" * self.max_size_status)

        self.emit(f"{self.h_arch:<{self.max_size_arch}}  ", end="")
        self.emit(f"{self.h_status:<{self.max_size_status}}")

        self.emit("=" * self.max_size_arch + "  ", end="")
        self.emit("=" * self.max_size_status)

        arch_table = self.data[feat]["table"]
        for arch in sorted(arch_table.keys()):
            self.emit(f"{arch:<{self.max_size_arch}}  ", end="")
            self.emit(f"{arch_table[arch]:<{self.max_size_status}}")

        self.emit("=" * self.max_size_arch + "  ", end="")
        self.emit("=" * self.max_size_status)

        return self.msg

    def matrix_lines(self, desc_size, max_size_status, header):
        """
        Helper function to split element tables at the output matrix
        """

        if header:
            ln_marker = "="
        else:
            ln_marker = "-"

        self.emit("+" + ln_marker * self.max_size_name + "+", end="")
        self.emit(ln_marker * desc_size, end="")
        self.emit("+" + ln_marker * max_size_status + "+")

    def output_matrix(self):
        """
        Generates a set of tables, groped by subsystem, containing
        what's the feature state on each architecture.
        """

        title = "Feature status on all architectures"

        self.emit("=" * len(title))
        self.emit(title)
        self.emit("=" * len(title))
        self.emit()

        desc_title = f"{self.h_kconfig} / {self.h_description}"

        desc_size = self.max_size_kconfig + 4
        if not self.description_size:
            desc_size = max(self.max_size_description, desc_size)
        else:
            desc_size = max(self.description_size, desc_size)

        desc_size = max(self.max_size_desc_word, desc_size, len(desc_title))

        notcompat = "Not compatible"
        self.max_size_status = max(self.max_size_status, len(notcompat))

        min_status_size = self.max_size_status + self.max_size_arch + 4
        max_size_status = max(min_status_size, self.max_size_status)

        h_status_per_arch = "Status per architecture"
        max_size_status = max(max_size_status, len(h_status_per_arch))

        cur_subsys = None
        for name in sorted(self.data.keys(),
                           key=lambda x: (self.data[x]["subsys"], x.lower())):
            if not cur_subsys or cur_subsys != self.data[name]["subsys"]:
                if cur_subsys:
                    self.emit()

                cur_subsys = self.data[name]["subsys"]

                title = f"Subsystem: {cur_subsys}"
                self.emit(title)
                self.emit("=" * len(title))
                self.emit()

                self.matrix_lines(desc_size, max_size_status, 0)

                self.emit(f"|{self.h_name:<{self.max_size_name}}", end="")
                self.emit(f"|{desc_title:<{desc_size}}", end="")
                self.emit(f"|{h_status_per_arch:<{max_size_status}}|")

                self.matrix_lines(desc_size, max_size_status, 1)

            lines = []
            descs = []
            cur_status = ""
            line = ""

            arch_table = sorted(self.data[name]["table"].items(),
                                key=lambda x: (self.status_map.get(x[1], 99),
                                               x[0].lower()))

            for arch, status in arch_table:
                if status == "---":
                    status = notcompat

                if status != cur_status:
                    if line != "":
                        lines.append(line)
                        line = ""
                    line = f"- **{status}**: {arch}"
                elif len(line) + len(arch) + 2 < max_size_status:
                    line += f", {arch}"
                else:
                    lines.append(line)
                    line = f"  {arch}"
                cur_status = status

            if line != "":
                lines.append(line)

            description = self.data[name]["description"]
            while len(description) > desc_size:
                desc_line = description[:desc_size]

                last_space = desc_line.rfind(" ")
                if last_space != -1:
                    desc_line = desc_line[:last_space]
                    descs.append(desc_line)
                    description = description[last_space + 1:]
                else:
                    desc_line = desc_line[:-1]
                    descs.append(desc_line + "\\")
                    description = description[len(desc_line):]

            if description:
                descs.append(description)

            while len(lines) < 2 + len(descs):
                lines.append("")

            for ln, line in enumerate(lines):
                col = ["", ""]

                if not ln:
                    col[0] = name
                    col[1] = f"``{self.data[name]['kconfig']}``"
                else:
                    if ln >= 2 and descs:
                        col[1] = descs.pop(0)

                self.emit(f"|{col[0]:<{self.max_size_name}}", end="")
                self.emit(f"|{col[1]:<{desc_size}}", end="")
                self.emit(f"|{line:<{max_size_status}}|")

            self.matrix_lines(desc_size, max_size_status, 0)

        return self.msg

    def list_arch_features(self, arch, feat):
        """
        Print a matrix of kernel feature support for the chosen architecture.
        """
        self.emit("#")
        self.emit(f"# Kernel feature support matrix of the '{arch}' architecture:")
        self.emit("#")

        # Sort by subsystem, then by feature name (case‑insensitive)
        for name in sorted(self.data.keys(),
                           key=lambda n: (self.data[n]["subsys"].lower(),
                                          n.lower())):
            if feat and name != feat:
                continue

            feature = self.data[name]
            arch_table = feature["table"]
            status = arch_table.get(arch, "")
            status = " " * ((4 - len(status)) // 2) + status

            self.emit(f"{feature['subsys']:>{self.max_size_subsys + 1}}/ ",
                      end="")
            self.emit(f"{name:<{self.max_size_name}}: ", end="")
            self.emit(f"{status:<5}|   ", end="")
            self.emit(f"{feature['kconfig']:>{self.max_size_kconfig}} ",
                      end="")
            self.emit(f"#  {feature['description']}")

        return self.msg
