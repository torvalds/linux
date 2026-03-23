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
        self.test_names = set()

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

    def output_symbols(self, fname, symbols):
        """
        Store source, symbols and output strings at self.tests.
        """

        #
        # KdocItem needs to be converted into dicts
        #
        kdoc_item = []
        expected = []

        #
        # Source code didn't produce any symbol
        #
        if not symbols:
            return

        expected_dict = {}
        start_line=1

        for arg in symbols:
            source = arg.get("source", "")

            if arg and "KdocItem" in self.yaml_content:
                msg = self.get_kdoc_item(arg)

                other_stuff = msg.get("other_stuff", {})
                if "source" in other_stuff:
                    del other_stuff["source"]

                expected_dict["kdoc_item"] = msg

            base_name = arg.name
            if not base_name:
                base_name = fname
            base_name = base_name.lower().replace(".", "_").replace("/", "_")


            # Don't add duplicated names
            i = 0
            name = base_name
            while name in self.test_names:
                i += 1
                name = f"{base_name}_{i:03d}"

            self.test_names.add(name)

            for out_style in self.out_style:
                if isinstance(out_style, ManFormat):
                    key = "man"
                else:
                    key = "rst"

                expected_dict[key]= out_style.output_symbols(fname, [arg]).strip()

            test = {
                "name": name,
                "description": f"{fname} line {arg.declaration_start_line}",
                "fname": fname,
                "source": source,
                "expected": [expected_dict]
            }

            self.tests.append(test)

            expected_dict = {}

    def write(self):
        """
        Output the content of self.tests to self.test_file.
        """
        import yaml

        # Helper function to better handle multilines
        def str_presenter(dumper, data):
            if "\n" in data:
                return dumper.represent_scalar("tag:yaml.org,2002:str", data, style="|")

            return dumper.represent_scalar("tag:yaml.org,2002:str", data)

        # Register the representer
        yaml.add_representer(str, str_presenter)

        data = {"tests": self.tests}

        with open(self.test_file, "w", encoding="utf-8") as fp:
            yaml.dump(data, fp,
                      sort_keys=False, width=120, indent=2,
                      default_flow_style=False, allow_unicode=True,
                      explicit_start=False, explicit_end=False)
