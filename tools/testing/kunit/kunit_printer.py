#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Utilities for printing and coloring output.
#
# Copyright (C) 2022, Google LLC.
# Author: Daniel Latypov <dlatypov@google.com>

import datetime
import sys
import typing

_RESET = '\033[0;0m'

class Printer:
	"""Wraps a file object, providing utilities for coloring output, etc."""

	def __init__(self, print: bool=True, output: typing.IO[str]=sys.stdout):
		self._output = output
		self._print = print
		if print:
			self._use_color = output.isatty()
		else:
			self._use_color = False

	def print(self, message: str) -> None:
		if self._print:
			print(message, file=self._output)

	def print_with_timestamp(self, message: str) -> None:
		ts = datetime.datetime.now().strftime('%H:%M:%S')
		self.print(f'[{ts}] {message}')

	def _color(self, code: str, text: str) -> str:
		if not self._use_color:
			return text
		return code + text + _RESET

	def red(self, text: str) -> str:
		return self._color('\033[1;31m', text)

	def yellow(self, text: str) -> str:
		return self._color('\033[1;33m', text)

	def green(self, text: str) -> str:
		return self._color('\033[1;32m', text)

	def color_len(self) -> int:
		"""Returns the length of the color escape codes."""
		return len(self.red(''))

# Provides a default instance that prints to stdout
stdout = Printer()
null_printer = Printer(print=False)
