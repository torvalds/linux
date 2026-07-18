# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import json
from typing import Any
from sbom.path_utils import PathStr
from sbom.spdx.core import SPDX_SPEC_VERSION, SpdxDocument, SpdxObject


class JsonLdSpdxDocument:
    """Represents an SPDX document in JSON-LD format for serialization."""

    graph: list[SpdxObject]

    def __init__(self, graph: list[SpdxObject]) -> None:
        """
        Initialize a JSON-LD SPDX document from a graph of SPDX objects.
        The graph must contain a single SpdxDocument element.

        Args:
            graph: List of SPDX objects representing the complete SPDX document.
        """
        self.graph = graph

    @property
    def context(self) -> list[str | dict[str, str]]:
        spdx_document = next(element for element in self.graph if isinstance(element, SpdxDocument))
        return [
            f"https://spdx.org/rdf/{SPDX_SPEC_VERSION}/spdx-context.jsonld",
            {ns.prefix: ns.namespace for ns in spdx_document.namespaceMap},
        ]

    def to_dict(self) -> dict[str, Any]:
        """
        Convert the SPDX document to a dictionary representation suitable for JSON serialization.

        Returns:
            Dictionary with @context and @graph keys following JSON-LD format.
        """
        def _item_to_dict(item: SpdxObject) -> dict:
            d = item.to_dict()
            if isinstance(item, SpdxDocument):
                d.pop("namespaceMap", None)
            return d
        return {
            "@context": self.context,
            "@graph": [_item_to_dict(item) for item in self.graph],
        }

    def save(self, path: PathStr, prettify: bool) -> None:
        """
        Save the SPDX document to a JSON file.

        Args:
            path: File path where the document will be saved.
            prettify: Whether to pretty-print the JSON with indentation.
        """
        with open(path, "w", encoding="utf-8") as f:
            if prettify:
                json.dump(self.to_dict(), f, indent=2)
            else:
                json.dump(self.to_dict(), f, separators=(",", ":"))
