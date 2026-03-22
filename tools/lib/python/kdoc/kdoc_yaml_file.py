#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2026: Mauro Carvalho Chehab <mchehab@kernel.org>.

import os

from kdoc.kdoc_output import ManFormat, RestFormat


class KDocTestFile():
    """
    Handles the logic needed to store kernel‑doc output inside a YAML file.
     Useful for unit tests and regression tests.
    """

    def __init__(self, config, yaml_file, yaml_content):
        #
        # Bail out early if yaml is not available
        #
        try:
            import yaml
        except ImportError:
            sys.exit("Warning: yaml package not available. Aborting it.")

        self.config = config
        self.test_file = os.path.expanduser(yaml_file)
        self.yaml_content = yaml_content

        self.tests = []

        out_dir = os.path.dirname(self.test_file)
        if out_dir and not os.path.isdir(out_dir):
            sys.exit(f"Directory {out_dir} doesn't exist.")

        self.out_style = []

        if "man" in self.yaml_content:
            out_style = ManFormat()
            out_style.set_config(self.config)

            self.out_style.append(out_style)

        if "rst" in self.yaml_content:
            out_style = RestFormat()
            out_style.set_config(self.config)

            self.out_style.append(out_style)

    def set_filter(self, export, internal, symbol, nosymbol,
                   function_table, enable_lineno, no_doc_sections):
        """
        Set filters at the output classes.
        """
        for out_style in self.out_style:
            out_style.set_filter(export, internal, symbol,
                                 nosymbol, function_table,
                                 enable_lineno, no_doc_sections)

    @staticmethod
    def get_kdoc_item(arg, start_line=1):

        d = vars(arg)

        declaration_start_line = d.get("declaration_start_line")
        if not declaration_start_line:
            return d

        d["declaration_start_line"] = start_line

        parameterdesc_start_lines = d.get("parameterdesc_start_lines")
        if parameterdesc_start_lines:
            for key in parameterdesc_start_lines:
                ln = parameterdesc_start_lines[key]
                ln += start_line - declaration_start_line

                parameterdesc_start_lines[key] = ln

        sections_start_lines = d.get("sections_start_lines")
        if sections_start_lines:
            for key in sections_start_lines:
                ln = sections_start_lines[key]
                ln += start_line - declaration_start_line

                sections_start_lines[key] = ln

        return d

    def output_symbols(self, fname, symbols, source):
        """
        Store source, symbols and output strings at self.tests.
        """

        #
        # KdocItem needs to be converted into dicts
        #
        kdoc_item = []
        expected = []

        if not symbols and not source:
            return

        if not source or len(symbols) != len(source):
            print(f"Warning: lengths are different. Ignoring {fname}")

            # Folding without line numbers is too hard.
            # The right thing to do here to proceed would be to delete
            # not-handled source blocks, as len(source) should be bigger
            # than len(symbols)
            return

        base_name = "test_" + fname.replace(".", "_").replace("/", "_")
        expected_dict = {}
        start_line=1

        for i in range(0, len(symbols)):
            arg = symbols[i]

            if "KdocItem" in self.yaml_content:
                msg = self.get_kdoc_item(arg)

                expected_dict["kdoc_item"] = msg

            for out_style in self.out_style:
                if isinstance(out_style, ManFormat):
                    key = "man"
                else:
                    key = "rst"

                expected_dict[key]= out_style.output_symbols(fname, [arg])

            name = f"{base_name}_{i:03d}"

            test = {
                "name": name,
                "description": f"{fname} line {source[i]["ln"]}",
                "fname": fname,
                "source": source[i]["data"],
                "expected": [expected_dict]
            }

            self.tests.append(test)

            expected_dict = {}

    def write(self):
        """
        Output the content of self.tests to self.test_file.
        """
        import yaml

        data = {"tests": self.tests}

        with open(self.test_file, "w", encoding="utf-8") as fp:
            yaml.safe_dump(data, fp, sort_keys=False, default_style="|",
                           default_flow_style=False, allow_unicode=True)
