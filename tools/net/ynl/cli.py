#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

import argparse
import json
import pprint
import time

from lib import YnlFamily, Netlink, NlError


class YnlEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, bytes):
            return bytes.hex(obj)
        if isinstance(obj, set):
            return list(obj)
        return json.JSONEncoder.default(self, obj)


def main():
    parser = argparse.ArgumentParser(description='YNL CLI sample')
    parser.add_argument('--spec', dest='spec', type=str, required=True)
    parser.add_argument('--schema', dest='schema', type=str)
    parser.add_argument('--no-schema', action='store_true')
    parser.add_argument('--json', dest='json_text', type=str)
    parser.add_argument('--do', dest='do', type=str)
    parser.add_argument('--dump', dest='dump', type=str)
    parser.add_argument('--sleep', dest='sleep', type=int)
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

    if args.sleep:
        time.sleep(args.sleep)

    try:
        if args.do:
            reply = ynl.do(args.do, attrs, args.flags)
            output(reply)
        if args.dump:
            reply = ynl.dump(args.dump, attrs)
            output(reply)
    except NlError as e:
        print(e)
        exit(1)

    if args.ntf:
        ynl.check_ntf()
        output(ynl.async_msg_queue)


if __name__ == "__main__":
    main()
