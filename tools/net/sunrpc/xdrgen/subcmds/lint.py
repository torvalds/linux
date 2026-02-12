#!/usr/bin/env python3
# ex: set filetype=python:

"""Translate an XDR specification into executable code that
can be compiled for the Linux kernel."""

import logging

from argparse import Namespace
from lark import logger
from lark.exceptions import VisitError

from xdr_parse import xdr_parser, make_error_handler, XdrParseError
from xdr_parse import handle_transform_error
from xdr_ast import transform_parse_tree

logger.setLevel(logging.DEBUG)


def subcmd(args: Namespace) -> int:
    """Lexical and syntax check of an XDR specification"""

    parser = xdr_parser()
    with open(args.filename, encoding="utf-8") as f:
        source = f.read()
        try:
            parse_tree = parser.parse(
                source, on_error=make_error_handler(source, args.filename)
            )
        except XdrParseError:
            return 1
        try:
            transform_parse_tree(parse_tree)
        except VisitError as e:
            handle_transform_error(e, source, args.filename)
            return 1

    return 0
