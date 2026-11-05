#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR pointer types"""

from jinja2 import Environment

from generators import SourceGenerator, kernel_c_type
from generators import create_jinja2_environment, get_jinja2_template

from xdr_ast import _XdrBasic, _XdrString
from xdr_ast import _XdrFixedLengthOpaque, _XdrVariableLengthOpaque
from xdr_ast import _XdrFixedLengthArray, _XdrVariableLengthArray
from xdr_ast import _XdrOptionalData, _XdrPointer, _XdrDeclaration
from xdr_ast import public_apis, get_header_name
from xdr_ast import pages_members, pages_member_maxsize, pages_member_is_decoded


def emit_pointer_declaration(environment: Environment, node: _XdrPointer) -> None:
    """Emit a declaration pair for an XDR pointer type"""
    if node.name in public_apis:
        template = get_jinja2_template(environment, "declaration", "close")
        print(template.render(name=node.name))


def emit_pointer_member_definition(
    environment: Environment, field: _XdrDeclaration, struct_name: str, peer: str
) -> None:
    """Emit a definition for one field in an XDR struct"""
    if peer == "server" and pages_member_is_decoded(struct_name, field.name):
        template = get_jinja2_template(environment, "definition", "pages_opaque")
        print(template.render(name=field.name))
        return
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


def emit_pointer_definition(
    environment: Environment, node: _XdrPointer, peer: str
) -> None:
    """Emit a definition for an XDR pointer type"""
    template = get_jinja2_template(environment, "definition", "open")
    print(template.render(name=node.name))

    for field in node.fields[0:-1]:
        emit_pointer_member_definition(environment, field, node.name, peer)

    template = get_jinja2_template(environment, "definition", "close")
    print(template.render(name=node.name))


def emit_pointer_member_decoder(
    environment: Environment,
    field: _XdrDeclaration,
    struct_name: str,
    peer: str,
) -> None:
    """Emit a decoder for one field in an XDR pointer"""
    if peer == "server" and pages_member_is_decoded(struct_name, field.name):
        template = get_jinja2_template(environment, "decoder", "pages_opaque")
        print(
            template.render(
                name=field.name,
                maxsize=pages_member_maxsize(field),
            )
        )
        return
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


def emit_pointer_decoder(
    environment: Environment, node: _XdrPointer, peer: str
) -> None:
    """Emit one decoder function for an XDR pointer type"""
    template = get_jinja2_template(environment, "decoder", "open")
    print(template.render(name=node.name))

    for field in node.fields[0:-1]:
        emit_pointer_member_decoder(environment, field, node.name, peer)

    template = get_jinja2_template(environment, "decoder", "close")
    print(template.render())


def emit_pointer_member_encoder(
    environment: Environment,
    field: _XdrDeclaration,
    struct_name: str,
    peer: str,
) -> None:
    """Emit an encoder for one field in a XDR pointer"""
    if (struct_name, field.name) in pages_members:
        if peer != "server":
            raise NotImplementedError(
                "pragma pages is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        # Both representations carry the length field, which is all the
        # pages encoder reads, so pages_member_is_decoded() does not
        # gate this site.
        template = get_jinja2_template(environment, "encoder", "pages_opaque")
        print(
            template.render(
                name=field.name,
                maxsize=pages_member_maxsize(field),
            )
        )
        return
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


def emit_pointer_encoder(
    environment: Environment, node: _XdrPointer, peer: str
) -> None:
    """Emit one encoder function for an XDR pointer type"""
    template = get_jinja2_template(environment, "encoder", "open")
    print(template.render(name=node.name))

    for field in node.fields[0:-1]:
        emit_pointer_member_encoder(environment, field, node.name, peer)

    template = get_jinja2_template(environment, "encoder", "close")
    print(template.render())


def emit_pointer_maxsize(environment: Environment, node: _XdrPointer) -> None:
    """Emit one maxsize macro for an XDR pointer type"""
    macro_name = get_header_name().upper() + "_" + node.name + "_sz"
    template = get_jinja2_template(environment, "maxsize", "pointer")
    print(
        template.render(
            macro=macro_name,
            width=" + ".join(node.symbolic_width()),
        )
    )


class XdrPointerGenerator(SourceGenerator):
    """Generate source code for XDR pointer"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "pointer")
        self.peer = peer

    def emit_declaration(self, node: _XdrPointer) -> None:
        """Emit one declaration pair for an XDR pointer type"""
        emit_pointer_declaration(self.environment, node)

    def emit_definition(self, node: _XdrPointer) -> None:
        """Emit one declaration for an XDR pointer type"""
        emit_pointer_definition(self.environment, node, self.peer)

    def emit_decoder(self, node: _XdrPointer) -> None:
        """Emit one decoder function for an XDR pointer type"""
        emit_pointer_decoder(self.environment, node, self.peer)

    def emit_encoder(self, node: _XdrPointer) -> None:
        """Emit one encoder function for an XDR pointer type"""
        emit_pointer_encoder(self.environment, node, self.peer)

    def emit_maxsize(self, node: _XdrPointer) -> None:
        """Emit one maxsize macro for an XDR pointer type"""
        emit_pointer_maxsize(self.environment, node)
