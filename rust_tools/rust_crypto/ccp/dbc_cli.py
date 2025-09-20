#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
import argparse
import binascii
import os
import errno
from dbc import *

ERRORS = {
    errno.EACCES: "Access is denied",
    errno.E2BIG: "Excess data provided",
    errno.EINVAL: "Bad parameters",
    errno.EAGAIN: "Bad state",
    errno.ENOENT: "Not implemented or message failure",
    errno.EBUSY: "Busy",
    errno.ENFILE: "Overflow",
    errno.EPERM: "Signature invalid",
}

messages = {
    "get-fmax-cap": PARAM_GET_FMAX_CAP,
    "set-fmax-cap": PARAM_SET_FMAX_CAP,
    "get-power-cap": PARAM_GET_PWR_CAP,
    "set-power-cap": PARAM_SET_PWR_CAP,
    "get-graphics-mode": PARAM_GET_GFX_MODE,
    "set-graphics-mode": PARAM_SET_GFX_MODE,
    "get-current-temp": PARAM_GET_CURR_TEMP,
    "get-fmax-max": PARAM_GET_FMAX_MAX,
    "get-fmax-min": PARAM_GET_FMAX_MIN,
    "get-soc-power-max": PARAM_GET_SOC_PWR_MAX,
    "get-soc-power-min": PARAM_GET_SOC_PWR_MIN,
    "get-soc-power-cur": PARAM_GET_SOC_PWR_CUR,
}


def _pretty_buffer(ba):
    return str(binascii.hexlify(ba, " "))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Dynamic Boost control command line interface"
    )
    parser.add_argument(
        "command",
        choices=["get-nonce", "get-param", "set-param", "set-uid"],
        help="Command to send",
    )
    parser.add_argument("--device", default="/dev/dbc", help="Device to operate")
    parser.add_argument("--signature", help="File containing signature for command")
    parser.add_argument("--message", choices=messages.keys(), help="Message index")
    parser.add_argument("--data", help="Argument to pass to message")
    parser.add_argument("--uid", help="File containing UID to pass")
    return parser.parse_args()


def pretty_error(code):
    if code in ERRORS:
        print(ERRORS[code])
    else:
        print("failed with return code %d" % code)


if __name__ == "__main__":
    args = parse_args()
    data = 0
    sig = None
    uid = None
    if not os.path.exists(args.device):
        raise IOError("Missing device {device}".format(device=args.device))
    if args.signature:
        if not os.path.exists(args.signature):
            raise ValueError("Invalid signature file %s" % args.signature)
        with open(args.signature, "rb") as f:
            sig = f.read()
        if len(sig) != DBC_SIG_SIZE:
            raise ValueError(
                "Invalid signature length %d (expected %d)" % (len(sig), DBC_SIG_SIZE)
            )
    if args.uid:
        if not os.path.exists(args.uid):
            raise ValueError("Invalid uid file %s" % args.uid)
        with open(args.uid, "rb") as f:
            uid = f.read()
        if len(uid) != DBC_UID_SIZE:
            raise ValueError(
                "Invalid UID length %d (expected %d)" % (len(uid), DBC_UID_SIZE)
            )
    if args.data:
        try:
            data = int(args.data, 10)
        except ValueError:
            data = int(args.data, 16)

    with open(args.device) as d:
        if args.command == "get-nonce":
            try:
                nonce = get_nonce(d, sig)
                print("Nonce: %s" % _pretty_buffer(bytes(nonce)))
            except OSError as e:
                pretty_error(e.errno)
        elif args.command == "set-uid":
            try:
                result = set_uid(d, uid, sig)
                if result:
                    print("Set UID")
            except OSError as e:
                pretty_error(e.errno)
        elif args.command == "get-param":
            if not args.message or args.message.startswith("set"):
                raise ValueError("Invalid message %s" % args.message)
            try:
                param, signature = process_param(d, messages[args.message], sig)
                print(
                    "Parameter: {par}, response signature {sig}".format(
                        par=param,
                        sig=_pretty_buffer(bytes(signature)),
                    )
                )
            except OSError as e:
                pretty_error(e.errno)
        elif args.command == "set-param":
            if not args.message or args.message.startswith("get"):
                raise ValueError("Invalid message %s" % args.message)
            try:
                param, signature = process_param(d, messages[args.message], sig, data)
                print(
                    "Parameter: {par}, response signature {sig}".format(
                        par=param,
                        sig=_pretty_buffer(bytes(signature)),
                    )
                )
            except OSError as e:
                pretty_error(e.errno)
