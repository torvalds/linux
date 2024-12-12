#!/usr/bin/env python3
# ex: set filetype=python:

"""Translate an XDR specification into executable code that
can be compiled for the Linux kernel."""

import logging

from argparse import Namespace
from lark import logger
from lark.exceptions import UnexpectedInput

from generators.source_top import XdrSourceTopGenerator
from generators.enum import XdrEnumGenerator
from generators.pointer import XdrPointerGenerator
from generators.program import XdrProgramGenerator
from generators.typedef import XdrTypedefGenerator
from generators.struct import XdrStructGenerator
from generators.union import XdrUnionGenerator

from xdr_ast import transform_parse_tree, _RpcProgram, Specification
from xdr_ast import _XdrAst, _XdrEnum, _XdrPointer
from xdr_ast import _XdrStruct, _XdrTypedef, _XdrUnion

from xdr_parse import xdr_parser, set_xdr_annotate

logger.setLevel(logging.INFO)


def emit_source_decoder(node: _XdrAst, language: str, peer: str) -> None:
    """Emit one XDR decoder function for a source file"""
    if isinstance(node, _XdrEnum):
        gen = XdrEnumGenerator(language, peer)
    elif isinstance(node, _XdrPointer):
        gen = XdrPointerGenerator(language, peer)
    elif isinstance(node, _XdrTypedef):
        gen = XdrTypedefGenerator(language, peer)
    elif isinstance(node, _XdrStruct):
        gen = XdrStructGenerator(language, peer)
    elif isinstance(node, _XdrUnion):
        gen = XdrUnionGenerator(language, peer)
    elif isinstance(node, _RpcProgram):
        gen = XdrProgramGenerator(language, peer)
    else:
        return
    gen.emit_decoder(node)


def emit_source_encoder(node: _XdrAst, language: str, peer: str) -> None:
    """Emit one XDR encoder function for a source file"""
    if isinstance(node, _XdrEnum):
        gen = XdrEnumGenerator(language, peer)
    elif isinstance(node, _XdrPointer):
        gen = XdrPointerGenerator(language, peer)
    elif isinstance(node, _XdrTypedef):
        gen = XdrTypedefGenerator(language, peer)
    elif isinstance(node, _XdrStruct):
        gen = XdrStructGenerator(language, peer)
    elif isinstance(node, _XdrUnion):
        gen = XdrUnionGenerator(language, peer)
    elif isinstance(node, _RpcProgram):
        gen = XdrProgramGenerator(language, peer)
    else:
        return
    gen.emit_encoder(node)


def generate_server_source(filename: str, root: Specification, language: str) -> None:
    """Generate server-side source code"""

    gen = XdrSourceTopGenerator(language, "server")
    gen.emit_source(filename, root)

    for definition in root.definitions:
        emit_source_decoder(definition.value, language, "server")
    for definition in root.definitions:
        emit_source_encoder(definition.value, language, "server")


def generate_client_source(filename: str, root: Specification, language: str) -> None:
    """Generate server-side source code"""

    gen = XdrSourceTopGenerator(language, "client")
    gen.emit_source(filename, root)

    # cel: todo: client needs XDR size macros

    for definition in root.definitions:
        emit_source_encoder(definition.value, language, "client")
    for definition in root.definitions:
        emit_source_decoder(definition.value, language, "client")

    # cel: todo: client needs PROC macros


def handle_parse_error(e: UnexpectedInput) -> bool:
    """Simple parse error reporting, no recovery attempted"""
    print(e)
    return True


def subcmd(args: Namespace) -> int:
    """Generate encoder and decoder functions"""

    set_xdr_annotate(args.annotate)
    parser = xdr_parser()
    with open(args.filename, encoding="utf-8") as f:
        parse_tree = parser.parse(f.read(), on_error=handle_parse_error)
        ast = transform_parse_tree(parse_tree)
        match args.peer:
            case "server":
                generate_server_source(args.filename, ast, args.language)
            case "client":
                generate_client_source(args.filename, ast, args.language)
            case _:
                print("Code generation for", args.peer, "is not yet supported")

    return 0
