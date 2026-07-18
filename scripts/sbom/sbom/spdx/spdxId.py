# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from itertools import count
from typing import Iterator

SpdxId = str


class SpdxIdGenerator:
    _namespace: str
    _prefix: str | None = None
    _counter: Iterator[int]

    def __init__(self, namespace: str, prefix: str | None = None) -> None:
        """
        Initialize the SPDX ID generator with a namespace.

        Args:
            namespace: The full namespace to use for generated IDs.
            prefix: Optional. If provided, generated IDs will use this prefix instead of the full namespace.
        """
        self._namespace = namespace
        self._prefix = prefix
        self._counter = count(0)

    def generate(self) -> SpdxId:
        return f"{f'{self._prefix}:' if self._prefix else self._namespace}{next(self._counter)}"

    @property
    def prefix(self) -> str | None:
        return self._prefix

    @property
    def namespace(self) -> str:
        return self._namespace
