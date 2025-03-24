#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

import argparse
import json
import pathlib
import pprint
import sys
import re
import os

sys.path.append(pathlib.Path(__file__).resolve().parent.as_posix())
from lib import YnlFamily
from cli import schema_dir, spec_dir

def args_to_req(ynl, op_name, args, req):
    """
    Verify and convert command-line arguments to the ynl-compatible request.
    """
    valid_attrs = ynl.operation_do_attributes(op_name)
    valid_attrs.remove('header') # not user-provided

    if len(args) == 0:
        print(f'no attributes, expected: {valid_attrs}')
        sys.exit(1)

    i = 0
    while i < len(args):
        attr = args[i]
        if i + 1 >= len(args):
            print(f'expected value for \'{attr}\'')
            sys.exit(1)

        if attr not in valid_attrs:
            print(f'invalid attribute \'{attr}\', expected: {valid_attrs}')
            sys.exit(1)

        val = args[i+1]
        i += 2

        req[attr] = val

def print_field(reply, *desc):
    """
    Pretty-print a set of fields from the reply. desc specifies the
    fields and the optional type (bool/yn).
    """
    if len(desc) == 0:
        return print_field(reply, *zip(reply.keys(), reply.keys()))

    for spec in desc:
        try:
            field, name, tp = spec
        except:
            field, name = spec
            tp = 'int'

        value = reply.get(field, None)
        if tp == 'yn':
            value = 'yes' if value else 'no'
        elif tp == 'bool' or isinstance(value, bool):
            value = 'on' if value else 'off'
        else:
            value = 'n/a' if value is None else value

        print(f'{name}: {value}')

def print_speed(name, value):
    """
    Print out the speed-like strings from the value dict.
    """
    speed_re = re.compile(r'[0-9]+base[^/]+/.+')
    speed = [ k for k, v in value.items() if v and speed_re.match(k) ]
    print(f'{name}: {" ".join(speed)}')

def doit(ynl, args, op_name):
    """
    Prepare request header, parse arguments and doit.
    """
    req = {
        'header': {
          'dev-name': args.device,
        },
    }

    args_to_req(ynl, op_name, args.args, req)
    ynl.do(op_name, req)

def dumpit(ynl, args, op_name, extra = {}):
    """
    Prepare request header, parse arguments and dumpit (filtering out the
    devices we're not interested in).
    """
    reply = ynl.dump(op_name, { 'header': {} } | extra)
    if not reply:
        return {}

    for msg in reply:
        if msg['header']['dev-name'] == args.device:
            if args.json:
                pprint.PrettyPrinter().pprint(msg)
                sys.exit(0)
            msg.pop('header', None)
            return msg

    print(f"Not supported for device {args.device}")
    sys.exit(1)

def bits_to_dict(attr):
    """
    Convert ynl-formatted bitmask to a dict of bit=value.
    """
    ret = {}
    if 'bits' not in attr:
        return dict()
    if 'bit' not in attr['bits']:
        return dict()
    for bit in attr['bits']['bit']:
        if bit['name'] == '':
            continue
        name = bit['name']
        value = bit.get('value', False)
        ret[name] = value
    return ret

def main():
    parser = argparse.ArgumentParser(description='ethtool wannabe')
    parser.add_argument('--json', action=argparse.BooleanOptionalAction)
    parser.add_argument('--show-priv-flags', action=argparse.BooleanOptionalAction)
    parser.add_argument('--set-priv-flags', action=argparse.BooleanOptionalAction)
    parser.add_argument('--show-eee', action=argparse.BooleanOptionalAction)
    parser.add_argument('--set-eee', action=argparse.BooleanOptionalAction)
    parser.add_argument('-a', '--show-pause', action=argparse.BooleanOptionalAction)
    parser.add_argument('-A', '--set-pause', action=argparse.BooleanOptionalAction)
    parser.add_argument('-c', '--show-coalesce', action=argparse.BooleanOptionalAction)
    parser.add_argument('-C', '--set-coalesce', action=argparse.BooleanOptionalAction)
    parser.add_argument('-g', '--show-ring', action=argparse.BooleanOptionalAction)
    parser.add_argument('-G', '--set-ring', action=argparse.BooleanOptionalAction)
    parser.add_argument('-k', '--show-features', action=argparse.BooleanOptionalAction)
    parser.add_argument('-K', '--set-features', action=argparse.BooleanOptionalAction)
    parser.add_argument('-l', '--show-channels', action=argparse.BooleanOptionalAction)
    parser.add_argument('-L', '--set-channels', action=argparse.BooleanOptionalAction)
    parser.add_argument('-T', '--show-time-stamping', action=argparse.BooleanOptionalAction)
    parser.add_argument('-S', '--statistics', action=argparse.BooleanOptionalAction)
    # TODO: --show-tunnels        tunnel-info-get
    # TODO: --show-module         module-get
    # TODO: --get-plca-cfg        plca-get
    # TODO: --get-plca-status     plca-get-status
    # TODO: --show-mm             mm-get
    # TODO: --show-fec            fec-get
    # TODO: --dump-module-eerpom  module-eeprom-get
    # TODO:                       pse-get
    # TODO:                       rss-get
    parser.add_argument('device', metavar='device', type=str)
    parser.add_argument('args', metavar='args', type=str, nargs='*')
    global args
    args = parser.parse_args()

    script_abs_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
    spec = os.path.join(spec_dir(), 'ethtool.yaml')
    schema = os.path.join(schema_dir(), 'genetlink-legacy.yaml')

    ynl = YnlFamily(spec, schema)

    if args.set_priv_flags:
        # TODO: parse the bitmask
        print("not implemented")
        return

    if args.set_eee:
        return doit(ynl, args, 'eee-set')

    if args.set_pause:
        return doit(ynl, args, 'pause-set')

    if args.set_coalesce:
        return doit(ynl, args, 'coalesce-set')

    if args.set_features:
        # TODO: parse the bitmask
        print("not implemented")
        return

    if args.set_channels:
        return doit(ynl, args, 'channels-set')

    if args.set_ring:
        return doit(ynl, args, 'rings-set')

    if args.show_priv_flags:
        flags = bits_to_dict(dumpit(ynl, args, 'privflags-get')['flags'])
        print_field(flags)
        return

    if args.show_eee:
        eee = dumpit(ynl, args, 'eee-get')
        ours = bits_to_dict(eee['modes-ours'])
        peer = bits_to_dict(eee['modes-peer'])

        if 'enabled' in eee:
            status = 'enabled' if eee['enabled'] else 'disabled'
            if 'active' in eee and eee['active']:
                status = status + ' - active'
            else:
                status = status + ' - inactive'
        else:
            status = 'not supported'

        print(f'EEE status: {status}')
        print_field(eee, ('tx-lpi-timer', 'Tx LPI'))
        print_speed('Advertised EEE link modes', ours)
        print_speed('Link partner advertised EEE link modes', peer)

        return

    if args.show_pause:
        print_field(dumpit(ynl, args, 'pause-get'),
                ('autoneg', 'Autonegotiate', 'bool'),
                ('rx', 'RX', 'bool'),
                ('tx', 'TX', 'bool'))
        return

    if args.show_coalesce:
        print_field(dumpit(ynl, args, 'coalesce-get'))
        return

    if args.show_features:
        reply = dumpit(ynl, args, 'features-get')
        available = bits_to_dict(reply['hw'])
        requested = bits_to_dict(reply['wanted']).keys()
        active = bits_to_dict(reply['active']).keys()
        never_changed = bits_to_dict(reply['nochange']).keys()

        for f in sorted(available):
            value = "off"
            if f in active:
                value = "on"

            fixed = ""
            if f not in available or f in never_changed:
                fixed = " [fixed]"

            req = ""
            if f in requested:
                if f in active:
                    req = " [requested on]"
                else:
                    req = " [requested off]"

            print(f'{f}: {value}{fixed}{req}')

        return

    if args.show_channels:
        reply = dumpit(ynl, args, 'channels-get')
        print(f'Channel parameters for {args.device}:')

        print(f'Pre-set maximums:')
        print_field(reply,
            ('rx-max', 'RX'),
            ('tx-max', 'TX'),
            ('other-max', 'Other'),
            ('combined-max', 'Combined'))

        print(f'Current hardware settings:')
        print_field(reply,
            ('rx-count', 'RX'),
            ('tx-count', 'TX'),
            ('other-count', 'Other'),
            ('combined-count', 'Combined'))

        return

    if args.show_ring:
        reply = dumpit(ynl, args, 'channels-get')

        print(f'Ring parameters for {args.device}:')

        print(f'Pre-set maximums:')
        print_field(reply,
            ('rx-max', 'RX'),
            ('rx-mini-max', 'RX Mini'),
            ('rx-jumbo-max', 'RX Jumbo'),
            ('tx-max', 'TX'))

        print(f'Current hardware settings:')
        print_field(reply,
            ('rx', 'RX'),
            ('rx-mini', 'RX Mini'),
            ('rx-jumbo', 'RX Jumbo'),
            ('tx', 'TX'))

        print_field(reply,
            ('rx-buf-len', 'RX Buf Len'),
            ('cqe-size', 'CQE Size'),
            ('tx-push', 'TX Push', 'bool'))

        return

    if args.statistics:
        print(f'NIC statistics:')

        # TODO: pass id?
        strset = dumpit(ynl, args, 'strset-get')
        pprint.PrettyPrinter().pprint(strset)

        req = {
          'groups': {
            'size': 1,
            'bits': {
              'bit':
                # TODO: support passing the bitmask
                #[
                  #{ 'name': 'eth-phy', 'value': True },
                  { 'name': 'eth-mac', 'value': True },
                  #{ 'name': 'eth-ctrl', 'value': True },
                  #{ 'name': 'rmon', 'value': True },
                #],
            },
          },
        }

        rsp = dumpit(ynl, args, 'stats-get', req)
        pprint.PrettyPrinter().pprint(rsp)
        return

    if args.show_time_stamping:
        req = {
          'header': {
            'flags': 'stats',
          },
        }

        tsinfo = dumpit(ynl, args, 'tsinfo-get', req)

        print(f'Time stamping parameters for {args.device}:')

        print('Capabilities:')
        [print(f'\t{v}') for v in bits_to_dict(tsinfo['timestamping'])]

        print(f'PTP Hardware Clock: {tsinfo["phc-index"]}')

        print('Hardware Transmit Timestamp Modes:')
        [print(f'\t{v}') for v in bits_to_dict(tsinfo['tx-types'])]

        print('Hardware Receive Filter Modes:')
        [print(f'\t{v}') for v in bits_to_dict(tsinfo['rx-filters'])]

        print('Statistics:')
        [print(f'\t{k}: {v}') for k, v in tsinfo['stats'].items()]
        return

    print(f'Settings for {args.device}:')
    linkmodes = dumpit(ynl, args, 'linkmodes-get')
    ours = bits_to_dict(linkmodes['ours'])

    supported_ports = ('TP',  'AUI', 'BNC', 'MII', 'FIBRE', 'Backplane')
    ports = [ p for p in supported_ports if ours.get(p, False)]
    print(f'Supported ports: [ {" ".join(ports)} ]')

    print_speed('Supported link modes', ours)

    print_field(ours, ('Pause', 'Supported pause frame use', 'yn'))
    print_field(ours, ('Autoneg', 'Supports auto-negotiation', 'yn'))

    supported_fec = ('None',  'PS', 'BASER', 'LLRS')
    fec = [ p for p in supported_fec if ours.get(p, False)]
    fec_str = " ".join(fec)
    if len(fec) == 0:
        fec_str = "Not reported"

    print(f'Supported FEC modes: {fec_str}')

    speed = 'Unknown!'
    if linkmodes['speed'] > 0 and linkmodes['speed'] < 0xffffffff:
        speed = f'{linkmodes["speed"]}Mb/s'
    print(f'Speed: {speed}')

    duplex_modes = {
            0: 'Half',
            1: 'Full',
    }
    duplex = duplex_modes.get(linkmodes["duplex"], None)
    if not duplex:
        duplex = f'Unknown! ({linkmodes["duplex"]})'
    print(f'Duplex: {duplex}')

    autoneg = "off"
    if linkmodes.get("autoneg", 0) != 0:
        autoneg = "on"
    print(f'Auto-negotiation: {autoneg}')

    ports = {
            0: 'Twisted Pair',
            1: 'AUI',
            2: 'MII',
            3: 'FIBRE',
            4: 'BNC',
            5: 'Directly Attached Copper',
            0xef: 'None',
    }
    linkinfo = dumpit(ynl, args, 'linkinfo-get')
    print(f'Port: {ports.get(linkinfo["port"], "Other")}')

    print_field(linkinfo, ('phyaddr', 'PHYAD'))

    transceiver = {
            0: 'Internal',
            1: 'External',
    }
    print(f'Transceiver: {transceiver.get(linkinfo["transceiver"], "Unknown")}')

    mdix_ctrl = {
            1: 'off',
            2: 'on',
    }
    mdix = mdix_ctrl.get(linkinfo['tp-mdix-ctrl'], None)
    if mdix:
        mdix = mdix + ' (forced)'
    else:
        mdix = mdix_ctrl.get(linkinfo['tp-mdix'], 'Unknown (auto)')
    print(f'MDI-X: {mdix}')

    debug = dumpit(ynl, args, 'debug-get')
    msgmask = bits_to_dict(debug.get("msgmask", [])).keys()
    print(f'Current message level: {" ".join(msgmask)}')

    linkstate = dumpit(ynl, args, 'linkstate-get')
    detected_states = {
            0: 'no',
            1: 'yes',
    }
    # TODO: wol-get
    detected = detected_states.get(linkstate['link'], 'unknown')
    print(f'Link detected: {detected}')

if __name__ == '__main__':
    main()
