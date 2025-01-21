#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR struct types"""

from jinja2 import Environment

from generators import SourceGenerator, kernel_c_type
from generators import create_jinja2_environment, get_jinja2_template

from xdr_ast import _XdrBasic, _XdrString
from xdr_ast import _XdrFixedLengthOpaque, _XdrVariableLengthOpaque
from xdr_ast import _XdrFixedLengthArray, _XdrVariableLengthArray
from xdr_ast import _XdrOptionalData, _XdrStruct, _XdrDeclaration
from xdr_ast import public_apis, get_header_name


def emit_struct_declaration(environment: Environment, node: _XdrStruct) -> None:
    """Emit one declaration pair for an XDR struct type"""
    if node.name in public_apis:
        template = get_jinja2_template(environment, "declaration", "close")
        print(template.render(name=node.name))


def emit_struct_member_definition(
    environment: Environment, field: _XdrDeclaration
) -> None:
    """Emit a definition for one field in an XDR struct"""
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "definition", field.template)
        print(template.render(name=field.name))
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "definition", field.template)
        print(template.render(name=field.name))
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_definition(environment: Environment, node: _XdrStruct) -> None:
    """Emit one definition for an XDR struct type"""
    template = get_jinja2_template(environment, "definition", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_definition(environment, field)

    template = get_jinja2_template(environment, "definition", "close")
    print(template.render(name=node.name))


def emit_struct_member_decoder(
    environment: Environment, field: _XdrDeclaration
) -> None:
    """Emit a decoder for one field in an XDR struct"""
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                size=field.size,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                maxsize=field.maxsize,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_decoder(environment: Environment, node: _XdrStruct) -> None:
    """Emit one decoder function for an XDR struct type"""
    template = get_jinja2_template(environment, "decoder", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_decoder(environment, field)

    template = get_jinja2_template(environment, "decoder", "close")
    print(template.render())


def emit_struct_member_encoder(
    environment: Environment, field: _XdrDeclaration
) -> None:
    """Emit an encoder for one field in an XDR struct"""
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_encoder(environment: Environment, node: _XdrStruct) -> None:
    """Emit one encoder function for an XDR struct type"""
    template = get_jinja2_template(environment, "encoder", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_encoder(environment, field)

    template = get_jinja2_template(environment, "encoder", "close")
    print(template.render())


def emit_struct_maxsize(environment: Environment, node: _XdrStruct) -> None:
    """Emit one maxsize macro for an XDR struct type"""
    macro_name = get_header_name().upper() + "_" + node.name + "_sz"
    template = get_jinja2_template(environment, "maxsize", "struct")
    print(
        template.render(
            macro=macro_name,
            width=" + ".join(node.symbolic_width()),
        )
    )


class XdrStructGenerator(SourceGenerator):
    """Generate source code for XDR structs"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "struct")
        self.peer = peer

    def emit_declaration(self, node: _XdrStruct) -> None:
        """Emit one declaration pair for an XDR struct type"""
        emit_struct_declaration(self.environment, node)

    def emit_definition(self, node: _XdrStruct) -> None:
        """Emit one definition for an XDR struct type"""
        emit_struct_definition(self.environment, node)

    def emit_decoder(self, node: _XdrStruct) -> None:
        """Emit one decoder function for an XDR struct type"""
        emit_struct_decoder(self.environment, node)

    def emit_encoder(self, node: _XdrStruct) -> None:
        """Emit one encoder function for an XDR struct type"""
        emit_struct_encoder(self.environment, node)

    def emit_maxsize(self, node: _XdrStruct) -> None:
        """Emit one maxsize macro for an XDR struct type"""
        emit_struct_maxsize(self.environment, node)
