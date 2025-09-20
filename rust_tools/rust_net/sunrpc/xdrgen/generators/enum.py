#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR enum types"""

from generators import SourceGenerator, create_jinja2_environment
from xdr_ast import _XdrEnum, public_apis, big_endian, get_header_name


class XdrEnumGenerator(SourceGenerator):
    """Generate source code for XDR enum types"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "enum")
        self.peer = peer

    def emit_declaration(self, node: _XdrEnum) -> None:
        """Emit one declaration pair for an XDR enum type"""
        if node.name in public_apis:
            template = self.environment.get_template("declaration/enum.j2")
            print(template.render(name=node.name))

    def emit_definition(self, node: _XdrEnum) -> None:
        """Emit one definition for an XDR enum type"""
        template = self.environment.get_template("definition/open.j2")
        print(template.render(name=node.name))

        template = self.environment.get_template("definition/enumerator.j2")
        for enumerator in node.enumerators:
            print(template.render(name=enumerator.name, value=enumerator.value))

        if node.name in big_endian:
            template = self.environment.get_template("definition/close_be.j2")
        else:
            template = self.environment.get_template("definition/close.j2")
        print(template.render(name=node.name))

    def emit_decoder(self, node: _XdrEnum) -> None:
        """Emit one decoder function for an XDR enum type"""
        if node.name in big_endian:
            template = self.environment.get_template("decoder/enum_be.j2")
        else:
            template = self.environment.get_template("decoder/enum.j2")
        print(template.render(name=node.name))

    def emit_encoder(self, node: _XdrEnum) -> None:
        """Emit one encoder function for an XDR enum type"""
        if node.name in big_endian:
            template = self.environment.get_template("encoder/enum_be.j2")
        else:
            template = self.environment.get_template("encoder/enum.j2")
        print(template.render(name=node.name))

    def emit_maxsize(self, node: _XdrEnum) -> None:
        """Emit one maxsize macro for an XDR enum type"""
        macro_name = get_header_name().upper() + "_" + node.name + "_sz"
        template = self.environment.get_template("maxsize/enum.j2")
        print(
            template.render(
                macro=macro_name,
                width=" + ".join(node.symbolic_width()),
            )
        )
