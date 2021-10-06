#!/usr/bin/env python3
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
#
# Copyright (C) 2021 Isovalent, Inc.

import argparse
import re
import os, sys

LINUX_ROOT = os.path.abspath(os.path.join(__file__,
    os.pardir, os.pardir, os.pardir, os.pardir, os.pardir))
BPFTOOL_DIR = os.path.join(LINUX_ROOT, 'tools/bpf/bpftool')
retval = 0

class BlockParser(object):
    """
    A parser for extracting set of values from blocks such as enums.
    @reader: a pointer to the open file to parse
    """
    def __init__(self, reader):
        self.reader = reader

    def search_block(self, start_marker):
        """
        Search for a given structure in a file.
        @start_marker: regex marking the beginning of a structure to parse
        """
        offset = self.reader.tell()
        array_start = re.search(start_marker, self.reader.read())
        if array_start is None:
            raise Exception('Failed to find start of block')
        self.reader.seek(offset + array_start.start())

    def parse(self, pattern, end_marker):
        """
        Parse a block and return a set of values. Values to extract must be
        on separate lines in the file.
        @pattern: pattern used to identify the values to extract
        @end_marker: regex marking the end of the block to parse
        """
        entries = set()
        while True:
            line = self.reader.readline()
            if not line or re.match(end_marker, line):
                break
            capture = pattern.search(line)
            if capture and pattern.groups >= 1:
                entries.add(capture.group(1))
        return entries

class ArrayParser(BlockParser):
    """
    A parser for extracting dicionaries of values from some BPF-related arrays.
    @reader: a pointer to the open file to parse
    @array_name: name of the array to parse
    """
    end_marker = re.compile('^};')

    def __init__(self, reader, array_name):
        self.array_name = array_name
        self.start_marker = re.compile(f'(static )?const char \* const {self.array_name}\[.*\] = {{\n')
        super().__init__(reader)

    def search_block(self):
        """
        Search for the given array in a file.
        """
        super().search_block(self.start_marker);

    def parse(self):
        """
        Parse a block and return data as a dictionary. Items to extract must be
        on separate lines in the file.
        """
        pattern = re.compile('\[(BPF_\w*)\]\s*= "(.*)",?$')
        entries = {}
        while True:
            line = self.reader.readline()
            if line == '' or re.match(self.end_marker, line):
                break
            capture = pattern.search(line)
            if capture:
                entries[capture.group(1)] = capture.group(2)
        return entries

class InlineListParser(BlockParser):
    """
    A parser for extracting set of values from inline lists.
    """
    def parse(self, pattern, end_marker):
        """
        Parse a block and return a set of values. Multiple values to extract
        can be on a same line in the file.
        @pattern: pattern used to identify the values to extract
        @end_marker: regex marking the end of the block to parse
        """
        entries = set()
        while True:
            line = self.reader.readline()
            if not line:
                break
            entries.update(pattern.findall(line))
            if re.search(end_marker, line):
                break
        return entries

class FileExtractor(object):
    """
    A generic reader for extracting data from a given file. This class contains
    several helper methods that wrap arround parser objects to extract values
    from different structures.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def __init__(self):
        self.reader = open(self.filename, 'r')

    def close(self):
        """
        Close the file used by the parser.
        """
        self.reader.close()

    def reset_read(self):
        """
        Reset the file position indicator for this parser. This is useful when
        parsing several structures in the file without respecting the order in
        which those structures appear in the file.
        """
        self.reader.seek(0)

    def get_types_from_array(self, array_name):
        """
        Search for and parse an array associating names to BPF_* enum members,
        for example:

            const char * const prog_type_name[] = {
                    [BPF_PROG_TYPE_UNSPEC]                  = "unspec",
                    [BPF_PROG_TYPE_SOCKET_FILTER]           = "socket_filter",
                    [BPF_PROG_TYPE_KPROBE]                  = "kprobe",
            };

        Return a dictionary with the enum member names as keys and the
        associated names as values, for example:

            {'BPF_PROG_TYPE_UNSPEC': 'unspec',
             'BPF_PROG_TYPE_SOCKET_FILTER': 'socket_filter',
             'BPF_PROG_TYPE_KPROBE': 'kprobe'}

        @array_name: name of the array to parse
        """
        array_parser = ArrayParser(self.reader, array_name)
        array_parser.search_block()
        return array_parser.parse()

    def get_enum(self, enum_name):
        """
        Search for and parse an enum containing BPF_* members, for example:

            enum bpf_prog_type {
                    BPF_PROG_TYPE_UNSPEC,
                    BPF_PROG_TYPE_SOCKET_FILTER,
                    BPF_PROG_TYPE_KPROBE,
            };

        Return a set containing all member names, for example:

            {'BPF_PROG_TYPE_UNSPEC',
             'BPF_PROG_TYPE_SOCKET_FILTER',
             'BPF_PROG_TYPE_KPROBE'}

        @enum_name: name of the enum to parse
        """
        start_marker = re.compile(f'enum {enum_name} {{\n')
        pattern = re.compile('^\s*(BPF_\w+),?$')
        end_marker = re.compile('^};')
        parser = BlockParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

    def __get_description_list(self, start_marker, pattern, end_marker):
        parser = InlineListParser(self.reader)
        parser.search_block(start_marker)
        return parser.parse(pattern, end_marker)

    def get_rst_list(self, block_name):
        """
        Search for and parse a list of type names from RST documentation, for
        example:

             |       *TYPE* := {
             |               **socket** | **kprobe** |
             |               **kretprobe**
             |       }

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'\*{block_name}\* := {{')
        pattern = re.compile('\*\*([\w/-]+)\*\*')
        end_marker = re.compile('}\n')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def get_help_list(self, block_name):
        """
        Search for and parse a list of type names from a help message in
        bpftool, for example:

            "       TYPE := { socket | kprobe |\\n"
            "               kretprobe }\\n"

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'"\s*{block_name} := {{')
        pattern = re.compile('([\w/]+) [|}]')
        end_marker = re.compile('}')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def get_help_list_macro(self, macro):
        """
        Search for and parse a list of values from a help message starting with
        a macro in bpftool, for example:

            "       " HELP_SPEC_OPTIONS " |\\n"
            "                    {-f|--bpffs} | {-m|--mapcompat} | {-n|--nomount} }\\n"

        Return a set containing all item names, for example:

            {'-f', '--bpffs', '-m', '--mapcompat', '-n', '--nomount'}

        @macro: macro starting the block, 'HELP_SPEC_OPTIONS' in the example
        """
        start_marker = re.compile(f'"\s*{macro}\s*" [|}}]')
        pattern = re.compile('([\w-]+) ?(?:\||}[ }\]])')
        end_marker = re.compile('}\\\\n')
        return self.__get_description_list(start_marker, pattern, end_marker)

    def default_options(self):
        """
        Return the default options contained in HELP_SPEC_OPTIONS
        """
        return { '-j', '--json', '-p', '--pretty', '-d', '--debug' }

    def get_bashcomp_list(self, block_name):
        """
        Search for and parse a list of type names from a variable in bash
        completion file, for example:

            local BPFTOOL_PROG_LOAD_TYPES='socket kprobe \\
                kretprobe'

        Return a set containing all type names, for example:

            {'socket', 'kprobe', 'kretprobe'}

        @block_name: name of the blog to parse, 'TYPE' in the example
        """
        start_marker = re.compile(f'local {block_name}=\'')
        pattern = re.compile('(?:.*=\')?([\w/]+)')
        end_marker = re.compile('\'$')
        return self.__get_description_list(start_marker, pattern, end_marker)

class SourceFileExtractor(FileExtractor):
    """
    An abstract extractor for a source file with usage message.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def get_options(self):
        return self.default_options().union(self.get_help_list_macro('HELP_SPEC_OPTIONS'))

class ProgFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's prog.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'prog.c')

    def get_prog_types(self):
        return self.get_types_from_array('prog_type_name')

    def get_attach_types(self):
        return self.get_types_from_array('attach_type_strings')

    def get_prog_attach_help(self):
        return self.get_help_list('ATTACH_TYPE')

class MapFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's map.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'map.c')

    def get_map_types(self):
        return self.get_types_from_array('map_type_name')

    def get_map_help(self):
        return self.get_help_list('TYPE')

class CgroupFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's cgroup.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'cgroup.c')

    def get_prog_attach_help(self):
        return self.get_help_list('ATTACH_TYPE')

class CommonFileExtractor(SourceFileExtractor):
    """
    An extractor for bpftool's common.c.
    """
    filename = os.path.join(BPFTOOL_DIR, 'common.c')

    def __init__(self):
        super().__init__()
        self.attach_types = {}

    def get_attach_types(self):
        if not self.attach_types:
            self.attach_types = self.get_types_from_array('attach_type_name')
        return self.attach_types

    def get_cgroup_attach_types(self):
        if not self.attach_types:
            self.get_attach_types()
        cgroup_types = {}
        for (key, value) in self.attach_types.items():
            if key.find('BPF_CGROUP') != -1:
                cgroup_types[key] = value
        return cgroup_types

class GenericSourceExtractor(SourceFileExtractor):
    """
    An extractor for generic source code files.
    """
    filename = ""

    def __init__(self, filename):
        self.filename = os.path.join(BPFTOOL_DIR, filename)
        super().__init__()

class BpfHeaderExtractor(FileExtractor):
    """
    An extractor for the UAPI BPF header.
    """
    filename = os.path.join(LINUX_ROOT, 'tools/include/uapi/linux/bpf.h')

    def get_prog_types(self):
        return self.get_enum('bpf_prog_type')

    def get_map_types(self):
        return self.get_enum('bpf_map_type')

    def get_attach_types(self):
        return self.get_enum('bpf_attach_type')

class ManPageExtractor(FileExtractor):
    """
    An abstract extractor for an RST documentation page.
    This class does not offer a way to set a filename, which is expected to be
    defined in children classes.
    """
    def get_options(self):
        return self.get_rst_list('OPTIONS')

class ManProgExtractor(ManPageExtractor):
    """
    An extractor for bpftool-prog.rst.
    """
    filename = os.path.join(BPFTOOL_DIR, 'Documentation/bpftool-prog.rst')

    def get_attach_types(self):
        return self.get_rst_list('ATTACH_TYPE')

class ManMapExtractor(ManPageExtractor):
    """
    An extractor for bpftool-map.rst.
    """
    filename = os.path.join(BPFTOOL_DIR, 'Documentation/bpftool-map.rst')

    def get_map_types(self):
        return self.get_rst_list('TYPE')

class ManCgroupExtractor(ManPageExtractor):
    """
    An extractor for bpftool-cgroup.rst.
    """
    filename = os.path.join(BPFTOOL_DIR, 'Documentation/bpftool-cgroup.rst')

    def get_attach_types(self):
        return self.get_rst_list('ATTACH_TYPE')

class ManGenericExtractor(ManPageExtractor):
    """
    An extractor for generic RST documentation pages.
    """
    filename = ""

    def __init__(self, filename):
        self.filename = os.path.join(BPFTOOL_DIR, filename)
        super().__init__()

class BashcompExtractor(FileExtractor):
    """
    An extractor for bpftool's bash completion file.
    """
    filename = os.path.join(BPFTOOL_DIR, 'bash-completion/bpftool')

    def get_prog_attach_types(self):
        return self.get_bashcomp_list('BPFTOOL_PROG_ATTACH_TYPES')

    def get_map_types(self):
        return self.get_bashcomp_list('BPFTOOL_MAP_CREATE_TYPES')

    def get_cgroup_attach_types(self):
        return self.get_bashcomp_list('BPFTOOL_CGROUP_ATTACH_TYPES')

def verify(first_set, second_set, message):
    """
    Print all values that differ between two sets.
    @first_set: one set to compare
    @second_set: another set to compare
    @message: message to print for values belonging to only one of the sets
    """
    global retval
    diff = first_set.symmetric_difference(second_set)
    if diff:
        print(message, diff)
        retval = 1

def main():
    # No arguments supported at this time, but print usage for -h|--help
    argParser = argparse.ArgumentParser(description="""
    Verify that bpftool's code, help messages, documentation and bash
    completion are all in sync on program types, map types, attach types, and
    options. Also check that bpftool is in sync with the UAPI BPF header.
    """)
    args = argParser.parse_args()

    # Map types (enum)

    bpf_info = BpfHeaderExtractor()
    ref = bpf_info.get_map_types()

    map_info = MapFileExtractor()
    source_map_items = map_info.get_map_types()
    map_types_enum = set(source_map_items.keys())

    verify(ref, map_types_enum,
            f'Comparing BPF header (enum bpf_map_type) and {MapFileExtractor.filename} (map_type_name):')

    # Map types (names)

    source_map_types = set(source_map_items.values())
    source_map_types.discard('unspec')

    help_map_types = map_info.get_map_help()
    help_map_options = map_info.get_options()
    map_info.close()

    man_map_info = ManMapExtractor()
    man_map_options = man_map_info.get_options()
    man_map_types = man_map_info.get_map_types()
    man_map_info.close()

    bashcomp_info = BashcompExtractor()
    bashcomp_map_types = bashcomp_info.get_map_types()

    verify(source_map_types, help_map_types,
            f'Comparing {MapFileExtractor.filename} (map_type_name) and {MapFileExtractor.filename} (do_help() TYPE):')
    verify(source_map_types, man_map_types,
            f'Comparing {MapFileExtractor.filename} (map_type_name) and {ManMapExtractor.filename} (TYPE):')
    verify(help_map_options, man_map_options,
            f'Comparing {MapFileExtractor.filename} (do_help() OPTIONS) and {ManMapExtractor.filename} (OPTIONS):')
    verify(source_map_types, bashcomp_map_types,
            f'Comparing {MapFileExtractor.filename} (map_type_name) and {BashcompExtractor.filename} (BPFTOOL_MAP_CREATE_TYPES):')

    # Program types (enum)

    ref = bpf_info.get_prog_types()

    prog_info = ProgFileExtractor()
    prog_types = set(prog_info.get_prog_types().keys())

    verify(ref, prog_types,
            f'Comparing BPF header (enum bpf_prog_type) and {ProgFileExtractor.filename} (prog_type_name):')

    # Attach types (enum)

    ref = bpf_info.get_attach_types()
    bpf_info.close()

    common_info = CommonFileExtractor()
    attach_types = common_info.get_attach_types()

    verify(ref, attach_types,
            f'Comparing BPF header (enum bpf_attach_type) and {CommonFileExtractor.filename} (attach_type_name):')

    # Attach types (names)

    source_prog_attach_types = set(prog_info.get_attach_types().values())

    help_prog_attach_types = prog_info.get_prog_attach_help()
    help_prog_options = prog_info.get_options()
    prog_info.close()

    man_prog_info = ManProgExtractor()
    man_prog_options = man_prog_info.get_options()
    man_prog_attach_types = man_prog_info.get_attach_types()
    man_prog_info.close()

    bashcomp_info.reset_read() # We stopped at map types, rewind
    bashcomp_prog_attach_types = bashcomp_info.get_prog_attach_types()

    verify(source_prog_attach_types, help_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (attach_type_strings) and {ProgFileExtractor.filename} (do_help() ATTACH_TYPE):')
    verify(source_prog_attach_types, man_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (attach_type_strings) and {ManProgExtractor.filename} (ATTACH_TYPE):')
    verify(help_prog_options, man_prog_options,
            f'Comparing {ProgFileExtractor.filename} (do_help() OPTIONS) and {ManProgExtractor.filename} (OPTIONS):')
    verify(source_prog_attach_types, bashcomp_prog_attach_types,
            f'Comparing {ProgFileExtractor.filename} (attach_type_strings) and {BashcompExtractor.filename} (BPFTOOL_PROG_ATTACH_TYPES):')

    # Cgroup attach types

    source_cgroup_attach_types = set(common_info.get_cgroup_attach_types().values())
    common_info.close()

    cgroup_info = CgroupFileExtractor()
    help_cgroup_attach_types = cgroup_info.get_prog_attach_help()
    help_cgroup_options = cgroup_info.get_options()
    cgroup_info.close()

    man_cgroup_info = ManCgroupExtractor()
    man_cgroup_options = man_cgroup_info.get_options()
    man_cgroup_attach_types = man_cgroup_info.get_attach_types()
    man_cgroup_info.close()

    bashcomp_cgroup_attach_types = bashcomp_info.get_cgroup_attach_types()
    bashcomp_info.close()

    verify(source_cgroup_attach_types, help_cgroup_attach_types,
            f'Comparing {CommonFileExtractor.filename} (attach_type_strings) and {CgroupFileExtractor.filename} (do_help() ATTACH_TYPE):')
    verify(source_cgroup_attach_types, man_cgroup_attach_types,
            f'Comparing {CommonFileExtractor.filename} (attach_type_strings) and {ManCgroupExtractor.filename} (ATTACH_TYPE):')
    verify(help_cgroup_options, man_cgroup_options,
            f'Comparing {CgroupFileExtractor.filename} (do_help() OPTIONS) and {ManCgroupExtractor.filename} (OPTIONS):')
    verify(source_cgroup_attach_types, bashcomp_cgroup_attach_types,
            f'Comparing {CommonFileExtractor.filename} (attach_type_strings) and {BashcompExtractor.filename} (BPFTOOL_CGROUP_ATTACH_TYPES):')

    # Options for remaining commands

    for cmd in [ 'btf', 'feature', 'gen', 'iter', 'link', 'net', 'perf', 'struct_ops', ]:
        source_info = GenericSourceExtractor(cmd + '.c')
        help_cmd_options = source_info.get_options()
        source_info.close()

        man_cmd_info = ManGenericExtractor(os.path.join('Documentation', 'bpftool-' + cmd + '.rst'))
        man_cmd_options = man_cmd_info.get_options()
        man_cmd_info.close()

        verify(help_cmd_options, man_cmd_options,
                f'Comparing {source_info.filename} (do_help() OPTIONS) and {man_cmd_info.filename} (OPTIONS):')

    source_main_info = GenericSourceExtractor('main.c')
    help_main_options = source_main_info.get_options()
    source_main_info.close()

    man_main_info = ManGenericExtractor(os.path.join('Documentation', 'bpftool.rst'))
    man_main_options = man_main_info.get_options()
    man_main_info.close()

    verify(help_main_options, man_main_options,
            f'Comparing {source_main_info.filename} (do_help() OPTIONS) and {man_main_info.filename} (OPTIONS):')

    sys.exit(retval)

if __name__ == "__main__":
    main()
