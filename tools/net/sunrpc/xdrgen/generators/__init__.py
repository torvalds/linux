# SPDX-License-Identifier: GPL-2.0

"""Define a base code generator class"""

import sys
from jinja2 import Environment, FileSystemLoader, Template

from xdr_ast import _XdrAst, Specification, _RpcProgram, _XdrTypeSpecifier
from xdr_ast import public_apis, pass_by_reference, get_header_name
from xdr_parse import get_xdr_annotate


def create_jinja2_environment(language: str, xdr_type: str) -> Environment:
    """Open a set of templates based on output language"""
    match language:
        case "C":
            environment = Environment(
                loader=FileSystemLoader(sys.path[0] + "/templates/C/" + xdr_type + "/"),
                trim_blocks=True,
                lstrip_blocks=True,
            )
            environment.globals["annotate"] = get_xdr_annotate()
            environment.globals["public_apis"] = public_apis
            environment.globals["pass_by_reference"] = pass_by_reference
            return environment
        case _:
            raise NotImplementedError("Language not supported")


def get_jinja2_template(
    environment: Environment, template_type: str, template_name: str
) -> Template:
    """Retrieve a Jinja2 template for emitting source code"""
    return environment.get_template(template_type + "/" + template_name + ".j2")


def find_xdr_program_name(root: Specification) -> str:
    """Retrieve the RPC program name from an abstract syntax tree"""
    raw_name = get_header_name()
    if raw_name != "none":
        return raw_name.lower()
    for definition in root.definitions:
        if isinstance(definition.value, _RpcProgram):
            raw_name = definition.value.name
            return raw_name.lower().removesuffix("_program").removesuffix("_prog")
    return "noprog"


def header_guard_infix(filename: str) -> str:
    """Extract the header guard infix from the specification filename"""
    basename = filename.split("/")[-1]
    program = basename.replace(".x", "")
    return program.upper()


def kernel_c_type(spec: _XdrTypeSpecifier) -> str:
    """Return name of C type"""
    builtin_native_c_type = {
        "bool": "bool",
        "int": "s32",
        "unsigned_int": "u32",
        "long": "s32",
        "unsigned_long": "u32",
        "hyper": "s64",
        "unsigned_hyper": "u64",
    }
    if spec.type_name in builtin_native_c_type:
        return builtin_native_c_type[spec.type_name]
    return spec.type_name


class Boilerplate:
    """Base class to generate boilerplate for source files"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        raise NotImplementedError("No language support defined")

    def emit_declaration(self, filename: str, root: Specification) -> None:
        """Emit declaration header boilerplate"""
        raise NotImplementedError("Header boilerplate generation not supported")

    def emit_definition(self, filename: str, root: Specification) -> None:
        """Emit definition header boilerplate"""
        raise NotImplementedError("Header boilerplate generation not supported")

    def emit_source(self, filename: str, root: Specification) -> None:
        """Emit generic source code for this XDR type"""
        raise NotImplementedError("Source boilerplate generation not supported")


class SourceGenerator:
    """Base class to generate header and source code for XDR types"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        raise NotImplementedError("No language support defined")

    def emit_declaration(self, node: _XdrAst) -> None:
        """Emit one function declaration for this XDR type"""
        raise NotImplementedError("Declaration generation not supported")

    def emit_decoder(self, node: _XdrAst) -> None:
        """Emit one decoder function for this XDR type"""
        raise NotImplementedError("Decoder generation not supported")

    def emit_definition(self, node: _XdrAst) -> None:
        """Emit one definition for this XDR type"""
        raise NotImplementedError("Definition generation not supported")

    def emit_encoder(self, node: _XdrAst) -> None:
        """Emit one encoder function for this XDR type"""
        raise NotImplementedError("Encoder generation not supported")

    def emit_maxsize(self, node: _XdrAst) -> None:
        """Emit one maxsize macro for this XDR type"""
        raise NotImplementedError("Maxsize macro generation not supported")
