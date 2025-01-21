#!/usr/bin/env python3
# ex: set filetype=python:

"""Translate an XDR specification into executable code that
can be compiled for the Linux kernel."""

import logging

from argparse import Namespace
from lark import logger
from lark.exceptions import UnexpectedInput

from generators.constant import XdrConstantGenerator
from generators.enum import XdrEnumGenerator
from generators.header_bottom import XdrHeaderBottomGenerator
from generators.header_top import XdrHeaderTopGenerator
from generators.pointer import XdrPointerGenerator
from generators.program import XdrProgramGenerator
from generators.typedef import XdrTypedefGenerator
from generators.struct import XdrStructGenerator
from generators.union import XdrUnionGenerator

from xdr_ast import transform_parse_tree, _RpcProgram, Specification
from xdr_ast import _XdrConstant, _XdrEnum, _XdrPointer
from xdr_ast import _XdrTypedef, _XdrStruct, _XdrUnion
from xdr_parse import xdr_parser, set_xdr_annotate

logger.setLevel(logging.INFO)


def emit_header_declarations(
    root: Specification, language: str, peer: str
) -> None:
    """Emit header declarations"""
    for definition in root.definitions:
        if isinstance(definition.value, _XdrEnum):
            gen = XdrEnumGenerator(language, peer)
        elif isinstance(definition.value, _XdrPointer):
            gen = XdrPointerGenerator(language, peer)
        elif isinstance(definition.value, _XdrTypedef):
            gen = XdrTypedefGenerator(language, peer)
        elif isinstance(definition.value, _XdrStruct):
            gen = XdrStructGenerator(language, peer)
        elif isinstance(definition.value, _XdrUnion):
            gen = XdrUnionGenerator(language, peer)
        elif isinstance(definition.value, _RpcProgram):
            gen = XdrProgramGenerator(language, peer)
        else:
            continue
        gen.emit_declaration(definition.value)


def handle_parse_error(e: UnexpectedInput) -> bool:
    """Simple parse error reporting, no recovery attempted"""
    print(e)
    return True


def subcmd(args: Namespace) -> int:
    """Generate definitions and declarations"""

    set_xdr_annotate(args.annotate)
    parser = xdr_parser()
    with open(args.filename, encoding="utf-8") as f:
        parse_tree = parser.parse(f.read(), on_error=handle_parse_error)
        ast = transform_parse_tree(parse_tree)

        gen = XdrHeaderTopGenerator(args.language, args.peer)
        gen.emit_declaration(args.filename, ast)

        emit_header_declarations(ast, args.language, args.peer)

        gen = XdrHeaderBottomGenerator(args.language, args.peer)
        gen.emit_declaration(args.filename, ast)

    return 0
