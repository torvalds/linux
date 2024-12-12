#!/usr/bin/env python3
# ex: set filetype=python:

"""Common parsing code for xdrgen"""

from lark import Lark


# Set to True to emit annotation comments in generated source
annotate = False


def set_xdr_annotate(set_it: bool) -> None:
    """Set 'annotate' if --annotate was specified on the command line"""
    global annotate
    annotate = set_it


def get_xdr_annotate() -> bool:
    """Return True if --annotate was specified on the command line"""
    return annotate


def xdr_parser() -> Lark:
    """Return a Lark parser instance configured with the XDR language grammar"""

    return Lark.open(
        "grammars/xdr.lark",
        rel_to=__file__,
        start="specification",
        debug=True,
        strict=True,
        propagate_positions=True,
        parser="lalr",
        lexer="contextual",
    )
