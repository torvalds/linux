#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# This file runs some basic checks to verify kunit works.
# It is only of interest if you're making changes to KUnit itself.
#
# Copyright (C) 2021, Google LLC.
# Author: Daniel Latypov <dlatypov@google.com.com>

from concurrent import futures
import datetime
import os
import shutil
import subprocess
import sys
import textwrap
from typing import Dict, List, Sequence

ABS_TOOL_PATH = os.path.abspath(os.path.dirname(__file__))
TIMEOUT = datetime.timedelta(minutes=5).total_seconds()

commands: Dict[str, Sequence[str]] = {
	'kunit_tool_test.py': ['./kunit_tool_test.py'],
	'kunit smoke test': ['./kunit.py', 'run', '--kunitconfig=lib/kunit', '--build_dir=kunit_run_checks'],
	'pytype': ['/bin/sh', '-c', 'pytype *.py'],
	'mypy': ['mypy', '--config-file', 'mypy.ini', '--exclude', '_test.py$', '--exclude', 'qemu_configs/', '.'],
}

# The user might not have mypy or pytype installed, skip them if so.
# Note: you can install both via `$ pip install mypy pytype`
necessary_deps : Dict[str, str] = {
	'pytype': 'pytype',
	'mypy': 'mypy',
}

def main(argv: Sequence[str]) -> None:
	if argv:
		raise RuntimeError('This script takes no arguments')

	future_to_name: Dict[futures.Future[None], str] = {}
	executor = futures.ThreadPoolExecutor(max_workers=len(commands))
	for name, argv in commands.items():
		if name in necessary_deps and shutil.which(necessary_deps[name]) is None:
			print(f'{name}: SKIPPED, {necessary_deps[name]} not in $PATH')
			continue
		f = executor.submit(run_cmd, argv)
		future_to_name[f] = name

	has_failures = False
	print(f'Waiting on {len(future_to_name)} checks ({", ".join(future_to_name.values())})...')
	for f in  futures.as_completed(future_to_name.keys()):
		name = future_to_name[f]
		ex = f.exception()
		if not ex:
			print(f'{name}: PASSED')
			continue

		has_failures = True
		if isinstance(ex, subprocess.TimeoutExpired):
			print(f'{name}: TIMED OUT')
		elif isinstance(ex, subprocess.CalledProcessError):
			print(f'{name}: FAILED')
		else:
			print(f'{name}: unexpected exception: {ex}')
			continue

		output = ex.output
		if output:
			print(textwrap.indent(output.decode(), '> '))
	executor.shutdown()

	if has_failures:
		sys.exit(1)


def run_cmd(argv: Sequence[str]) -> None:
	subprocess.check_output(argv, stderr=subprocess.STDOUT, cwd=ABS_TOOL_PATH, timeout=TIMEOUT)


if __name__ == '__main__':
	main(sys.argv[1:])
