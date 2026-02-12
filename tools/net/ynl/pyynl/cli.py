#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

"""
YNL cli tool
"""

import argparse
import json
import os
import pathlib
import pprint
import shutil
import sys
import textwrap

# pylint: disable=no-name-in-module,wrong-import-position
sys.path.append(pathlib.Path(__file__).resolve().parent.as_posix())
from lib import YnlFamily, Netlink, NlError, SpecFamily, SpecException, YnlException

SYS_SCHEMA_DIR='/usr/share/ynl'
RELATIVE_SCHEMA_DIR='../../../../Documentation/netlink'

# pylint: disable=too-few-public-methods,too-many-locals
class Colors:
    """ANSI color and font modifier codes"""
    RESET = '\033[0m'

    BOLD = '\033[1m'
    ITALICS = '\033[3m'
    UNDERLINE = '\033[4m'
    INVERT = '\033[7m'


def color(text, modifiers):
    """Add color to text if output is a TTY

    Returns:
        Colored text if stdout is a TTY, otherwise plain text
    """
    if sys.stdout.isatty():
        # Join the colors if they are a list, if it's a string this a noop
        modifiers = "".join(modifiers)
        return f"{modifiers}{text}{Colors.RESET}"
    return text

def term_width():
    """ Get terminal width in columns (80 if stdout is not a terminal) """
    return shutil.get_terminal_size().columns

def schema_dir():
    """
    Return the effective schema directory, preferring in-tree before
    system schema directory.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    schema_dir_ = os.path.abspath(f"{script_dir}/{RELATIVE_SCHEMA_DIR}")
    if not os.path.isdir(schema_dir_):
        schema_dir_ = SYS_SCHEMA_DIR
    if not os.path.isdir(schema_dir_):
        raise YnlException(f"Schema directory {schema_dir_} does not exist")
    return schema_dir_

def spec_dir():
    """
    Return the effective spec directory, relative to the effective
    schema directory.
    """
    spec_dir_ = schema_dir() + '/specs'
    if not os.path.isdir(spec_dir_):
        raise YnlException(f"Spec directory {spec_dir_} does not exist")
    return spec_dir_


class YnlEncoder(json.JSONEncoder):
    """A custom encoder for emitting JSON with ynl-specific instance types"""
    def default(self, o):
        if isinstance(o, bytes):
            return bytes.hex(o)
        if isinstance(o, set):
            return list(o)
        return json.JSONEncoder.default(self, o)


def print_attr_list(ynl, attr_names, attr_set, indent=2):
    """Print a list of attributes with their types and documentation."""
    prefix = ' ' * indent
    for attr_name in attr_names:
        if attr_name in attr_set.attrs:
            attr = attr_set.attrs[attr_name]
            attr_info = f'{prefix}- {color(attr_name, Colors.BOLD)}: {attr.type}'
            if 'enum' in attr.yaml:
                enum_name = attr.yaml['enum']
                attr_info += f" (enum: {enum_name})"
                # Print enum values if available
                if enum_name in ynl.consts:
                    const = ynl.consts[enum_name]
                    enum_values = list(const.entries.keys())
                    type_fmted = color(const.type.capitalize(), Colors.ITALICS)
                    attr_info += f"\n{prefix}  {type_fmted}: {', '.join(enum_values)}"

            # Show nested attributes reference and recursively display them
            nested_set_name = None
            if attr.type == 'nest' and 'nested-attributes' in attr.yaml:
                nested_set_name = attr.yaml['nested-attributes']
                attr_info += f" -> {nested_set_name}"

            if attr.yaml.get('doc'):
                doc_prefix = prefix + ' ' * 4
                doc_text = textwrap.fill(attr.yaml['doc'], width=term_width(),
                                         initial_indent=doc_prefix,
                                         subsequent_indent=doc_prefix)
                attr_info += f"\n{doc_text}"
            print(attr_info)

            # Recursively show nested attributes
            if nested_set_name in ynl.attr_sets:
                nested_set = ynl.attr_sets[nested_set_name]
                # Filter out 'unspec' and other unused attrs
                nested_names = [n for n in nested_set.attrs.keys()
                                if nested_set.attrs[n].type != 'unused']
                if nested_names:
                    print_attr_list(ynl, nested_names, nested_set, indent + 4)


def print_mode_attrs(ynl, mode, mode_spec, attr_set, consistent_dd_reply=None):
    """Print a given mode (do/dump/event/notify)."""
    mode_title = mode.capitalize()

    if 'request' in mode_spec and 'attributes' in mode_spec['request']:
        print(f'\n{mode_title} request attributes:')
        print_attr_list(ynl, mode_spec['request']['attributes'], attr_set)

    if 'reply' in mode_spec and 'attributes' in mode_spec['reply']:
        if consistent_dd_reply and mode == "do":
            title = None  # Dump handling will print in combined format
        elif consistent_dd_reply and mode == "dump":
            title = 'Do and Dump'
        else:
            title = f'{mode_title}'
        if title:
            print(f'\n{title} reply attributes:')
            print_attr_list(ynl, mode_spec['reply']['attributes'], attr_set)


def do_doc(ynl, op):
    """Handle --list-attrs $op, print the attr information to stdout"""
    print(f'Operation: {color(op.name, Colors.BOLD)}')
    print(op.yaml['doc'])

    consistent_dd_reply = False
    if 'do' in op.yaml and 'dump' in op.yaml and 'reply' in op.yaml['do'] and \
       op.yaml['do']['reply'] == op.yaml['dump'].get('reply'):
        consistent_dd_reply = True

    for mode in ['do', 'dump']:
        if mode in op.yaml:
            print_mode_attrs(ynl, mode, op.yaml[mode], op.attr_set,
                             consistent_dd_reply=consistent_dd_reply)

    if 'attributes' in op.yaml.get('event', {}):
        print('\nEvent attributes:')
        print_attr_list(ynl, op.yaml['event']['attributes'], op.attr_set)

    if 'notify' in op.yaml:
        mode_spec = op.yaml['notify']
        ref_spec = ynl.msgs.get(mode_spec).yaml.get('do')
        if not ref_spec:
            ref_spec = ynl.msgs.get(mode_spec).yaml.get('dump')
        if ref_spec:
            print('\nNotification attributes:')
            print_attr_list(ynl, ref_spec['reply']['attributes'], op.attr_set)

    if 'mcgrp' in op.yaml:
        print(f"\nMulticast group: {op.yaml['mcgrp']}")


# pylint: disable=too-many-locals,too-many-branches,too-many-statements
def main():
    """YNL cli tool"""

    description = """
    YNL CLI utility - a general purpose netlink utility that uses YAML
    specs to drive protocol encoding and decoding.
    """
    epilog = """
    The --multi option can be repeated to include several do operations
    in the same netlink payload.
    """

    parser = argparse.ArgumentParser(description=description,
                                     epilog=epilog, add_help=False)

    gen_group = parser.add_argument_group('General options')
    gen_group.add_argument('-h', '--help', action='help',
                           help='show this help message and exit')

    spec_group = parser.add_argument_group('Netlink family selection')
    spec_sel = spec_group.add_mutually_exclusive_group(required=True)
    spec_sel.add_argument('--list-families', action='store_true',
                          help=('list Netlink families supported by YNL '
                                '(which have a spec available in the standard '
                                'system path)'))
    spec_sel.add_argument('--family', dest='family', type=str,
                          help='name of the Netlink FAMILY to use')
    spec_sel.add_argument('--spec', dest='spec', type=str,
                          help='full file path to the YAML spec file')

    ops_group = parser.add_argument_group('Operations')
    ops = ops_group.add_mutually_exclusive_group()
    ops.add_argument('--do', dest='do', metavar='DO-OPERATION', type=str)
    ops.add_argument('--dump', dest='dump', metavar='DUMP-OPERATION', type=str)
    ops.add_argument('--multi', dest='multi', nargs=2, action='append',
                     metavar=('DO-OPERATION', 'JSON_TEXT'), type=str,
                     help="Multi-message operation sequence (for nftables)")
    ops.add_argument('--list-ops', action='store_true',
                     help="List available --do and --dump operations")
    ops.add_argument('--list-msgs', action='store_true',
                     help="List all messages of the family (incl. notifications)")
    ops.add_argument('--list-attrs', '--doc', dest='list_attrs', metavar='MSG',
                     type=str, help='List attributes for a message / operation')
    ops.add_argument('--validate', action='store_true',
                     help="Validate the spec against schema and exit")

    io_group = parser.add_argument_group('Input / Output')
    io_group.add_argument('--json', dest='json_text', type=str,
                          help=('Specify attributes of the message to send '
                                'to the kernel in JSON format. Can be left out '
                                'if the message is expected to be empty.'))
    io_group.add_argument('--output-json', action='store_true',
                          help='Format output as JSON')

    ntf_group = parser.add_argument_group('Notifications')
    ntf_group.add_argument('--subscribe', dest='ntf', type=str)
    ntf_group.add_argument('--duration', dest='duration', type=int,
                           help='when subscribed, watch for DURATION seconds')
    ntf_group.add_argument('--sleep', dest='duration', type=int,
                           help='alias for duration')

    nlflags = parser.add_argument_group('Netlink message flags (NLM_F_*)',
                                        ('Extra flags to set in nlmsg_flags of '
                                         'the request, used mostly by older '
                                         'Classic Netlink families.'))
    nlflags.add_argument('--replace', dest='flags', action='append_const',
                         const=Netlink.NLM_F_REPLACE)
    nlflags.add_argument('--excl', dest='flags', action='append_const',
                         const=Netlink.NLM_F_EXCL)
    nlflags.add_argument('--create', dest='flags', action='append_const',
                         const=Netlink.NLM_F_CREATE)
    nlflags.add_argument('--append', dest='flags', action='append_const',
                         const=Netlink.NLM_F_APPEND)

    schema_group = parser.add_argument_group('Development options')
    schema_group.add_argument('--schema', dest='schema', type=str,
                              help="JSON schema to validate the spec")
    schema_group.add_argument('--no-schema', action='store_true')

    dbg_group = parser.add_argument_group('Debug options')
    dbg_group.add_argument('--dbg-small-recv', default=0, const=4000,
                           action='store', nargs='?', type=int, metavar='INT',
                           help="Length of buffers used for recv()")
    dbg_group.add_argument('--process-unknown', action=argparse.BooleanOptionalAction)

    args = parser.parse_args()

    def output(msg):
        if args.output_json:
            print(json.dumps(msg, cls=YnlEncoder))
        else:
            pprint.pprint(msg, width=term_width(), compact=True)

    if args.list_families:
        for filename in sorted(os.listdir(spec_dir())):
            if filename.endswith('.yaml'):
                print(filename.removesuffix('.yaml'))
        return

    if args.no_schema:
        args.schema = ''

    attrs = {}
    if args.json_text:
        attrs = json.loads(args.json_text)

    if args.family:
        spec = f"{spec_dir()}/{args.family}.yaml"
    else:
        spec = args.spec
    if not os.path.isfile(spec):
        raise YnlException(f"Spec file {spec} does not exist")

    if args.validate:
        try:
            SpecFamily(spec, args.schema)
        except SpecException as error:
            print(error)
            sys.exit(1)
        return

    if args.family: # set behaviour when using installed specs
        if args.schema is None and spec.startswith(SYS_SCHEMA_DIR):
            args.schema = '' # disable schema validation when installed
        if args.process_unknown is None:
            args.process_unknown = True

    ynl = YnlFamily(spec, args.schema, args.process_unknown,
                    recv_size=args.dbg_small_recv)
    if args.dbg_small_recv:
        ynl.set_recv_dbg(True)

    if args.ntf:
        ynl.ntf_subscribe(args.ntf)

    if args.list_ops:
        for op_name, op in ynl.ops.items():
            print(op_name, " [", ", ".join(op.modes), "]")
    if args.list_msgs:
        for op_name, op in ynl.msgs.items():
            print(op_name, " [", ", ".join(op.modes), "]")

    if args.list_attrs:
        op = ynl.msgs.get(args.list_attrs)
        if not op:
            print(f'Operation {args.list_attrs} not found')
            sys.exit(1)

        do_doc(ynl, op)

    try:
        if args.do:
            reply = ynl.do(args.do, attrs, args.flags)
            output(reply)
        if args.dump:
            reply = ynl.dump(args.dump, attrs)
            output(reply)
        if args.multi:
            ops = [ (item[0], json.loads(item[1]), args.flags or []) for item in args.multi ]
            reply = ynl.do_multi(ops)
            output(reply)

        if args.ntf:
            for msg in ynl.poll_ntf(duration=args.duration):
                output(msg)
    except NlError as e:
        print(e)
        sys.exit(1)
    except KeyboardInterrupt:
        pass
    except BrokenPipeError:
        pass


if __name__ == "__main__":
    main()
