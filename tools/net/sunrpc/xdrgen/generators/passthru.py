#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code for XDR pass-through lines"""

from generators import SourceGenerator, create_jinja2_environment
from xdr_ast import _XdrPassthru


class XdrPassthruGenerator(SourceGenerator):
    """Generate source code for XDR pass-through content"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "passthru")
        self.peer = peer

    def emit_definition(self, node: _XdrPassthru) -> None:
        """Emit one pass-through line"""
        template = self.environment.get_template("definition.j2")
        print(template.render(content=node.content))

    def emit_decoder(self, node: _XdrPassthru) -> None:
        """Emit one pass-through line"""
        template = self.environment.get_template("source.j2")
        print(template.render(content=node.content))
