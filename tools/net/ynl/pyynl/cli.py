#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

import argparse
import json
import pathlib
import pprint
import sys

sys.path.append(pathlib.Path(__file__).resolve().parent.as_posix())
from lib import YnlFamily, Netlink, NlError


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
    parser.add_argument('--spec', dest='spec', type=str, required=True)
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

    if args.no_schema:
        args.schema = ''

    attrs = {}
    if args.json_text:
        attrs = json.loads(args.json_text)

    ynl = YnlFamily(args.spec, args.schema, args.process_unknown,
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
    except NlError as e:
        print(e)
        exit(1)

    if args.ntf:
        try:
            for msg in ynl.poll_ntf(duration=args.duration):
                output(msg)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
