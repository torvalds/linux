#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8; mode: python -*-

"""
    Class to auto generate the documentation for Netlink specifications.

    :copyright:  Copyright (C) 2023  Breno Leitao <leitao@debian.org>
    :license:    GPL Version 2, June 1991 see linux/COPYING for details.

    This class performs extensive parsing to the Linux kernel's netlink YAML
    spec files, in an effort to avoid needing to heavily mark up the original
    YAML file.

    This code is split in two classes:
        1) RST formatters: Use to convert a string to a RST output
        2) YAML Netlink (YNL) doc generator: Generate docs from YAML data
"""

from typing import Any, Dict, List
import yaml

LINE_STR = '__lineno__'

class NumberedSafeLoader(yaml.SafeLoader):              # pylint: disable=R0901
    """Override the SafeLoader class to add line number to parsed data"""

    def construct_mapping(self, node, *args, **kwargs):
        mapping = super().construct_mapping(node, *args, **kwargs)
        mapping[LINE_STR] = node.start_mark.line

        return mapping

class RstFormatters:
    """RST Formatters"""

    SPACE_PER_LEVEL = 4

    @staticmethod
    def headroom(level: int) -> str:
        """Return space to format"""
        return " " * (level * RstFormatters.SPACE_PER_LEVEL)

    @staticmethod
    def bold(text: str) -> str:
        """Format bold text"""
        return f"**{text}**"

    @staticmethod
    def inline(text: str) -> str:
        """Format inline text"""
        return f"``{text}``"

    @staticmethod
    def sanitize(text: str) -> str:
        """Remove newlines and multiple spaces"""
        # This is useful for some fields that are spread across multiple lines
        return str(text).replace("\n", " ").strip()

    def rst_fields(self, key: str, value: str, level: int = 0) -> str:
        """Return a RST formatted field"""
        return self.headroom(level) + f":{key}: {value}"

    def rst_definition(self, key: str, value: Any, level: int = 0) -> str:
        """Format a single rst definition"""
        return self.headroom(level) + key + "\n" + self.headroom(level + 1) + str(value)

    def rst_paragraph(self, paragraph: str, level: int = 0) -> str:
        """Return a formatted paragraph"""
        return self.headroom(level) + paragraph

    def rst_bullet(self, item: str, level: int = 0) -> str:
        """Return a formatted a bullet"""
        return self.headroom(level) + f"- {item}"

    @staticmethod
    def rst_subsection(title: str) -> str:
        """Add a sub-section to the document"""
        return f"{title}\n" + "-" * len(title)

    @staticmethod
    def rst_subsubsection(title: str) -> str:
        """Add a sub-sub-section to the document"""
        return f"{title}\n" + "~" * len(title)

    @staticmethod
    def rst_section(namespace: str, prefix: str, title: str) -> str:
        """Add a section to the document"""
        return f".. _{namespace}-{prefix}-{title}:\n\n{title}\n" + "=" * len(title)

    @staticmethod
    def rst_subtitle(title: str) -> str:
        """Add a subtitle to the document"""
        return "\n" + "-" * len(title) + f"\n{title}\n" + "-" * len(title) + "\n\n"

    @staticmethod
    def rst_title(title: str) -> str:
        """Add a title to the document"""
        return "=" * len(title) + f"\n{title}\n" + "=" * len(title) + "\n\n"

    def rst_list_inline(self, list_: List[str], level: int = 0) -> str:
        """Format a list using inlines"""
        return self.headroom(level) + "[" + ", ".join(self.inline(i) for i in list_) + "]"

    @staticmethod
    def rst_ref(namespace: str, prefix: str, name: str) -> str:
        """Add a hyperlink to the document"""
        mappings = {'enum': 'definition',
                    'fixed-header': 'definition',
                    'nested-attributes': 'attribute-set',
                    'struct': 'definition'}
        if prefix in mappings:
            prefix = mappings[prefix]
        return f":ref:`{namespace}-{prefix}-{name}`"

    def rst_header(self) -> str:
        """The headers for all the auto generated RST files"""
        lines = []

        lines.append(self.rst_paragraph(".. SPDX-License-Identifier: GPL-2.0"))
        lines.append(self.rst_paragraph(".. NOTE: This document was auto-generated.\n\n"))

        return "\n".join(lines)

    @staticmethod
    def rst_toctree(maxdepth: int = 2) -> str:
        """Generate a toctree RST primitive"""
        lines = []

        lines.append(".. toctree::")
        lines.append(f"   :maxdepth: {maxdepth}\n\n")

        return "\n".join(lines)

    @staticmethod
    def rst_label(title: str) -> str:
        """Return a formatted label"""
        return f".. _{title}:\n\n"

    @staticmethod
    def rst_lineno(lineno: int) -> str:
        """Return a lineno comment"""
        return f".. LINENO {lineno}\n"

class YnlDocGenerator:
    """YAML Netlink specs Parser"""

    fmt = RstFormatters()

    def parse_mcast_group(self, mcast_group: List[Dict[str, Any]]) -> str:
        """Parse 'multicast' group list and return a formatted string"""
        lines = []
        for group in mcast_group:
            lines.append(self.fmt.rst_bullet(group["name"]))

        return "\n".join(lines)

    def parse_do(self, do_dict: Dict[str, Any], level: int = 0) -> str:
        """Parse 'do' section and return a formatted string"""
        lines = []
        if LINE_STR in do_dict:
            lines.append(self.fmt.rst_lineno(do_dict[LINE_STR]))

        for key in do_dict.keys():
            if key == LINE_STR:
                continue
            lines.append(self.fmt.rst_paragraph(self.fmt.bold(key), level + 1))
            if key in ['request', 'reply']:
                lines.append(self.parse_do_attributes(do_dict[key], level + 1) + "\n")
            else:
                lines.append(self.fmt.headroom(level + 2) + do_dict[key] + "\n")

        return "\n".join(lines)

    def parse_do_attributes(self, attrs: Dict[str, Any], level: int = 0) -> str:
        """Parse 'attributes' section"""
        if "attributes" not in attrs:
            return ""
        lines = [self.fmt.rst_fields("attributes",
                                     self.fmt.rst_list_inline(attrs["attributes"]),
                                     level + 1)]

        return "\n".join(lines)

    def parse_operations(self, operations: List[Dict[str, Any]], namespace: str) -> str:
        """Parse operations block"""
        preprocessed = ["name", "doc", "title", "do", "dump", "flags"]
        linkable = ["fixed-header", "attribute-set"]
        lines = []

        for operation in operations:
            if LINE_STR in operation:
                lines.append(self.fmt.rst_lineno(operation[LINE_STR]))

            lines.append(self.fmt.rst_section(namespace, 'operation',
                                              operation["name"]))
            lines.append(self.fmt.rst_paragraph(operation["doc"]) + "\n")

            for key in operation.keys():
                if key == LINE_STR:
                    continue

                if key in preprocessed:
                    # Skip the special fields
                    continue
                value = operation[key]
                if key in linkable:
                    value = self.fmt.rst_ref(namespace, key, value)
                lines.append(self.fmt.rst_fields(key, value, 0))
            if 'flags' in operation:
                lines.append(self.fmt.rst_fields('flags',
                                                 self.fmt.rst_list_inline(operation['flags'])))

            if "do" in operation:
                lines.append(self.fmt.rst_paragraph(":do:", 0))
                lines.append(self.parse_do(operation["do"], 0))
            if "dump" in operation:
                lines.append(self.fmt.rst_paragraph(":dump:", 0))
                lines.append(self.parse_do(operation["dump"], 0))

            # New line after fields
            lines.append("\n")

        return "\n".join(lines)

    def parse_entries(self, entries: List[Dict[str, Any]], level: int) -> str:
        """Parse a list of entries"""
        ignored = ["pad"]
        lines = []
        for entry in entries:
            if isinstance(entry, dict):
                # entries could be a list or a dictionary
                field_name = entry.get("name", "")
                if field_name in ignored:
                    continue
                type_ = entry.get("type")
                if type_:
                    field_name += f" ({self.fmt.inline(type_)})"
                lines.append(
                    self.fmt.rst_fields(field_name,
                                        self.fmt.sanitize(entry.get("doc", "")),
                                        level)
                )
            elif isinstance(entry, list):
                lines.append(self.fmt.rst_list_inline(entry, level))
            else:
                lines.append(self.fmt.rst_bullet(self.fmt.inline(self.fmt.sanitize(entry)),
                                                 level))

        lines.append("\n")
        return "\n".join(lines)

    def parse_definitions(self, defs: Dict[str, Any], namespace: str) -> str:
        """Parse definitions section"""
        preprocessed = ["name", "entries", "members"]
        ignored = ["render-max"]  # This is not printed
        lines = []

        for definition in defs:
            if LINE_STR in definition:
                lines.append(self.fmt.rst_lineno(definition[LINE_STR]))

            lines.append(self.fmt.rst_section(namespace, 'definition', definition["name"]))
            for k in definition.keys():
                if k == LINE_STR:
                    continue
                if k in preprocessed + ignored:
                    continue
                lines.append(self.fmt.rst_fields(k, self.fmt.sanitize(definition[k]), 0))

            # Field list needs to finish with a new line
            lines.append("\n")
            if "entries" in definition:
                lines.append(self.fmt.rst_paragraph(":entries:", 0))
                lines.append(self.parse_entries(definition["entries"], 1))
            if "members" in definition:
                lines.append(self.fmt.rst_paragraph(":members:", 0))
                lines.append(self.parse_entries(definition["members"], 1))

        return "\n".join(lines)

    def parse_attr_sets(self, entries: List[Dict[str, Any]], namespace: str) -> str:
        """Parse attribute from attribute-set"""
        preprocessed = ["name", "type"]
        linkable = ["enum", "nested-attributes", "struct", "sub-message"]
        ignored = ["checks"]
        lines = []

        for entry in entries:
            lines.append(self.fmt.rst_section(namespace, 'attribute-set',
                                              entry["name"]))

            if "doc" in entry:
                lines.append(self.fmt.rst_paragraph(entry["doc"], 0) + "\n")

            for attr in entry["attributes"]:
                if LINE_STR in attr:
                    lines.append(self.fmt.rst_lineno(attr[LINE_STR]))

                type_ = attr.get("type")
                attr_line = attr["name"]
                if type_:
                    # Add the attribute type in the same line
                    attr_line += f" ({self.fmt.inline(type_)})"

                lines.append(self.fmt.rst_subsubsection(attr_line))

                for k in attr.keys():
                    if k == LINE_STR:
                        continue
                    if k in preprocessed + ignored:
                        continue
                    if k in linkable:
                        value = self.fmt.rst_ref(namespace, k, attr[k])
                    else:
                        value = self.fmt.sanitize(attr[k])
                    lines.append(self.fmt.rst_fields(k, value, 0))
                lines.append("\n")

        return "\n".join(lines)

    def parse_sub_messages(self, entries: List[Dict[str, Any]], namespace: str) -> str:
        """Parse sub-message definitions"""
        lines = []

        for entry in entries:
            lines.append(self.fmt.rst_section(namespace, 'sub-message',
                                              entry["name"]))
            for fmt in entry["formats"]:
                value = fmt["value"]

                lines.append(self.fmt.rst_bullet(self.fmt.bold(value)))
                for attr in ['fixed-header', 'attribute-set']:
                    if attr in fmt:
                        lines.append(self.fmt.rst_fields(attr,
                                                         self.fmt.rst_ref(namespace,
                                                                          attr,
                                                                          fmt[attr]),
                                                         1))
                lines.append("\n")

        return "\n".join(lines)

    def parse_yaml(self, obj: Dict[str, Any]) -> str:
        """Format the whole YAML into a RST string"""
        lines = []

        # Main header
        lineno = obj.get('__lineno__', 0)
        lines.append(self.fmt.rst_lineno(lineno))

        family = obj['name']

        lines.append(self.fmt.rst_header())
        lines.append(self.fmt.rst_label("netlink-" + family))

        title = f"Family ``{family}`` netlink specification"
        lines.append(self.fmt.rst_title(title))
        lines.append(self.fmt.rst_paragraph(".. contents:: :depth: 3\n"))

        if "doc" in obj:
            lines.append(self.fmt.rst_subtitle("Summary"))
            lines.append(self.fmt.rst_paragraph(obj["doc"], 0))

        # Operations
        if "operations" in obj:
            lines.append(self.fmt.rst_subtitle("Operations"))
            lines.append(self.parse_operations(obj["operations"]["list"],
                                               family))

        # Multicast groups
        if "mcast-groups" in obj:
            lines.append(self.fmt.rst_subtitle("Multicast groups"))
            lines.append(self.parse_mcast_group(obj["mcast-groups"]["list"]))

        # Definitions
        if "definitions" in obj:
            lines.append(self.fmt.rst_subtitle("Definitions"))
            lines.append(self.parse_definitions(obj["definitions"], family))

        # Attributes set
        if "attribute-sets" in obj:
            lines.append(self.fmt.rst_subtitle("Attribute sets"))
            lines.append(self.parse_attr_sets(obj["attribute-sets"], family))

        # Sub-messages
        if "sub-messages" in obj:
            lines.append(self.fmt.rst_subtitle("Sub-messages"))
            lines.append(self.parse_sub_messages(obj["sub-messages"], family))

        return "\n".join(lines)

    # Main functions
    # ==============

    def parse_yaml_file(self, filename: str) -> str:
        """Transform the YAML specified by filename into an RST-formatted string"""
        with open(filename, "r", encoding="utf-8") as spec_file:
            numbered_yaml = yaml.load(spec_file, Loader=NumberedSafeLoader)
            content = self.parse_yaml(numbered_yaml)

        return content
