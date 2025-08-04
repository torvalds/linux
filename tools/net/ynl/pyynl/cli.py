#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

import argparse
import json
import os
import pathlib
import pprint
import sys

sys.path.append(pathlib.Path(__file__).resolve().parent.as_posix())
from lib import YnlFamily, Netlink, NlError

sys_schema_dir='/usr/share/ynl'
relative_schema_dir='../../../../Documentation/netlink'

def schema_dir():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    schema_dir = os.path.abspath(f"{script_dir}/{relative_schema_dir}")
    if not os.path.isdir(schema_dir):
        schema_dir = sys_schema_dir
    if not os.path.isdir(schema_dir):
        raise Exception(f"Schema directory {schema_dir} does not exist")
    return schema_dir

def spec_dir():
    spec_dir = schema_dir() + '/specs'
    if not os.path.isdir(spec_dir):
        raise Exception(f"Spec directory {spec_dir} does not exist")
    return spec_dir


class YnlEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, bytes):
            return bytes.hex(obj)
        if isinstance(obj, set):
            return list(obj)
        return json.JSONEncoder.default(self, obj)


def main():
    description = """
    YNL CLI utility - a general purpose netlink utility that uses YAML
    specs to drive protocol encoding and decoding.
    """
    epilog = """
    The --multi option can be repeated to include several do operations
    in the same netlink payload.
    """

    parser = argparse.ArgumentParser(description=description,
                                     epilog=epilog)
    spec_group = parser.add_mutually_exclusive_group(required=True)
    spec_group.add_argument('--family', dest='family', type=str,
                            help='name of the netlink FAMILY')
    spec_group.add_argument('--list-families', action='store_true',
                            help='list all netlink families supported by YNL (has spec)')
    spec_group.add_argument('--spec', dest='spec', type=str,
                            help='choose the family by SPEC file path')

    parser.add_argument('--schema', dest='schema', type=str)
    parser.add_argument('--no-schema', action='store_true')
    parser.add_argument('--json', dest='json_text', type=str)

    group = parser.add_mutually_exclusive_group()
    group.add_argument('--do', dest='do', metavar='DO-OPERATION', type=str)
    group.add_argument('--multi', dest='multi', nargs=2, action='append',
                       metavar=('DO-OPERATION', 'JSON_TEXT'), type=str)
    group.add_argument('--dump', dest='dump', metavar='DUMP-OPERATION', type=str)
    group.add_argument('--list-ops', action='store_true')
    group.add_argument('--list-msgs', action='store_true')

    parser.add_argument('--duration', dest='duration', type=int,
                        help='when subscribed, watch for DURATION seconds')
    parser.add_argument('--sleep', dest='duration', type=int,
                        help='alias for duration')
    parser.add_argument('--subscribe', dest='ntf', type=str)
    parser.add_argument('--replace', dest='flags', action='append_const',
                        const=Netlink.NLM_F_REPLACE)
    parser.add_argument('--excl', dest='flags', action='append_const',
                        const=Netlink.NLM_F_EXCL)
    parser.add_argument('--create', dest='flags', action='append_const',
                        const=Netlink.NLM_F_CREATE)
    parser.add_argument('--append', dest='flags', action='append_const',
                        const=Netlink.NLM_F_APPEND)
    parser.add_argument('--process-unknown', action=argparse.BooleanOptionalAction)
    parser.add_argument('--output-json', action='store_true')
    parser.add_argument('--dbg-small-recv', default=0, const=4000,
                        action='store', nargs='?', type=int)
    args = parser.parse_args()

    def output(msg):
        if args.output_json:
            print(json.dumps(msg, cls=YnlEncoder))
        else:
            pprint.PrettyPrinter().pprint(msg)

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
        if args.schema is None and spec.startswith(sys_schema_dir):
            args.schema = '' # disable schema validation when installed
    else:
        spec = args.spec
    if not os.path.isfile(spec):
        raise Exception(f"Spec file {spec} does not exist")

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
        exit(1)
    except KeyboardInterrupt:
        pass
    except BrokenPipeError:
        pass


if __name__ == "__main__":
    main()
