# SPDX-License-Identifier: GPL-2.0
#
# Runs UML kernel, collects output, and handles errors.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import importlib.util
import logging
import subprocess
import os
import shutil
import signal
from typing import Iterator, Optional, Tuple

from contextlib import ExitStack

from collections import namedtuple

import kunit_config
import kunit_parser
import qemu_config

KCONFIG_PATH = '.config'
KUNITCONFIG_PATH = '.kunitconfig'
DEFAULT_KUNITCONFIG_PATH = 'tools/testing/kunit/configs/default.config'
BROKEN_ALLCONFIG_PATH = 'tools/testing/kunit/configs/broken_on_uml.config'
OUTFILE_PATH = 'test.log'
ABS_TOOL_PATH = os.path.abspath(os.path.dirname(__file__))
QEMU_CONFIGS_DIR = os.path.join(ABS_TOOL_PATH, 'qemu_configs')

def get_file_path(build_dir, default):
	if build_dir:
		default = os.path.join(build_dir, default)
	return default

class ConfigError(Exception):
	"""Represents an error trying to configure the Linux kernel."""


class BuildError(Exception):
	"""Represents an error trying to build the Linux kernel."""


class LinuxSourceTreeOperations(object):
	"""An abstraction over command line operations performed on a source tree."""

	def __init__(self, linux_arch: str, cross_compile: Optional[str]):
		self._linux_arch = linux_arch
		self._cross_compile = cross_compile

	def make_mrproper(self) -> None:
		try:
			subprocess.check_output(['make', 'mrproper'], stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make_arch_qemuconfig(self, kconfig: kunit_config.Kconfig) -> None:
		pass

	def make_allyesconfig(self, build_dir, make_options) -> None:
		raise ConfigError('Only the "um" arch is supported for alltests')

	def make_olddefconfig(self, build_dir, make_options) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, 'olddefconfig']
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		print('Populating config with:\n$', ' '.join(command))
		try:
			subprocess.check_output(command, stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make(self, jobs, build_dir, make_options) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, '--jobs=' + str(jobs)]
		if make_options:
			command.extend(make_options)
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
		if build_dir:
			command += ['O=' + build_dir]
		print('Building with:\n$', ' '.join(command))
		try:
			proc = subprocess.Popen(command,
						stderr=subprocess.PIPE,
						stdout=subprocess.DEVNULL)
		except OSError as e:
			raise BuildError('Could not call execute make: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise BuildError(e.output)
		_, stderr = proc.communicate()
		if proc.returncode != 0:
			raise BuildError(stderr.decode())
		if stderr:  # likely only due to build warnings
			print(stderr.decode())

	def run(self, params, timeout, build_dir, outfile) -> None:
		pass


class LinuxSourceTreeOperationsQemu(LinuxSourceTreeOperations):

	def __init__(self, qemu_arch_params: qemu_config.QemuArchParams, cross_compile: Optional[str]):
		super().__init__(linux_arch=qemu_arch_params.linux_arch,
				 cross_compile=cross_compile)
		self._kconfig = qemu_arch_params.kconfig
		self._qemu_arch = qemu_arch_params.qemu_arch
		self._kernel_path = qemu_arch_params.kernel_path
		self._kernel_command_line = qemu_arch_params.kernel_command_line + ' kunit_shutdown=reboot'
		self._extra_qemu_params = qemu_arch_params.extra_qemu_params

	def make_arch_qemuconfig(self, base_kunitconfig: kunit_config.Kconfig) -> None:
		kconfig = kunit_config.Kconfig()
		kconfig.parse_from_string(self._kconfig)
		base_kunitconfig.merge_in_entries(kconfig)

	def run(self, params, timeout, build_dir, outfile):
		kernel_path = os.path.join(build_dir, self._kernel_path)
		qemu_command = ['qemu-system-' + self._qemu_arch,
				'-nodefaults',
				'-m', '1024',
				'-kernel', kernel_path,
				'-append', '\'' + ' '.join(params + [self._kernel_command_line]) + '\'',
				'-no-reboot',
				'-nographic',
				'-serial stdio'] + self._extra_qemu_params
		print('Running tests with:\n$', ' '.join(qemu_command))
		with open(outfile, 'w') as output:
			process = subprocess.Popen(' '.join(qemu_command),
						   stdin=subprocess.PIPE,
						   stdout=output,
						   stderr=subprocess.STDOUT,
						   text=True, shell=True)
		try:
			process.wait(timeout=timeout)
		except Exception as e:
			print(e)
			process.terminate()
		return process

class LinuxSourceTreeOperationsUml(LinuxSourceTreeOperations):
	"""An abstraction over command line operations performed on a source tree."""

	def __init__(self, cross_compile=None):
		super().__init__(linux_arch='um', cross_compile=cross_compile)

	def make_allyesconfig(self, build_dir, make_options) -> None:
		kunit_parser.print_with_timestamp(
			'Enabling all CONFIGs for UML...')
		command = ['make', 'ARCH=um', 'allyesconfig']
		if make_options:
			command.extend(make_options)
		if build_dir:
			command += ['O=' + build_dir]
		process = subprocess.Popen(
			command,
			stdout=subprocess.DEVNULL,
			stderr=subprocess.STDOUT)
		process.wait()
		kunit_parser.print_with_timestamp(
			'Disabling broken configs to run KUnit tests...')
		with ExitStack() as es:
			config = open(get_kconfig_path(build_dir), 'a')
			disable = open(BROKEN_ALLCONFIG_PATH, 'r').read()
			config.write(disable)
		kunit_parser.print_with_timestamp(
			'Starting Kernel with all configs takes a few minutes...')

	def run(self, params, timeout, build_dir, outfile):
		"""Runs the Linux UML binary. Must be named 'linux'."""
		linux_bin = get_file_path(build_dir, 'linux')
		outfile = get_outfile_path(build_dir)
		with open(outfile, 'w') as output:
			process = subprocess.Popen([linux_bin] + params,
						   stdin=subprocess.PIPE,
						   stdout=output,
						   stderr=subprocess.STDOUT,
						   text=True)
			process.wait(timeout)

def get_kconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KCONFIG_PATH)

def get_kunitconfig_path(build_dir) -> str:
	return get_file_path(build_dir, KUNITCONFIG_PATH)

def get_outfile_path(build_dir) -> str:
	return get_file_path(build_dir, OUTFILE_PATH)

def get_source_tree_ops(arch: str, cross_compile: Optional[str]) -> LinuxSourceTreeOperations:
	config_path = os.path.join(QEMU_CONFIGS_DIR, arch + '.py')
	if arch == 'um':
		return LinuxSourceTreeOperationsUml(cross_compile=cross_compile)
	elif os.path.isfile(config_path):
		return get_source_tree_ops_from_qemu_config(config_path, cross_compile)[1]
	else:
		raise ConfigError(arch + ' is not a valid arch')

def get_source_tree_ops_from_qemu_config(config_path: str,
					 cross_compile: Optional[str]) -> Tuple[
							 str, LinuxSourceTreeOperations]:
	# The module name/path has very little to do with where the actual file
	# exists (I learned this through experimentation and could not find it
	# anywhere in the Python documentation).
	#
	# Bascially, we completely ignore the actual file location of the config
	# we are loading and just tell Python that the module lives in the
	# QEMU_CONFIGS_DIR for import purposes regardless of where it actually
	# exists as a file.
	module_path = '.' + os.path.join(os.path.basename(QEMU_CONFIGS_DIR), os.path.basename(config_path))
	spec = importlib.util.spec_from_file_location(module_path, config_path)
	config = importlib.util.module_from_spec(spec)
	# TODO(brendanhiggins@google.com): I looked this up and apparently other
	# Python projects have noted that pytype complains that "No attribute
	# 'exec_module' on _importlib_modulespec._Loader". Disabling for now.
	spec.loader.exec_module(config) # pytype: disable=attribute-error
	return config.QEMU_ARCH.linux_arch, LinuxSourceTreeOperationsQemu(
			config.QEMU_ARCH, cross_compile=cross_compile)

class LinuxSourceTree(object):
	"""Represents a Linux kernel source tree with KUnit tests."""

	def __init__(
	      self,
	      build_dir: str,
	      load_config=True,
	      kunitconfig_path='',
	      arch=None,
	      cross_compile=None,
	      qemu_config_path=None) -> None:
		signal.signal(signal.SIGINT, self.signal_handler)
		if qemu_config_path:
			self._arch, self._ops = get_source_tree_ops_from_qemu_config(
					qemu_config_path, cross_compile)
		else:
			self._arch = 'um' if arch is None else arch
			self._ops = get_source_tree_ops(self._arch, cross_compile)

		if not load_config:
			return

		if kunitconfig_path:
			if os.path.isdir(kunitconfig_path):
				kunitconfig_path = os.path.join(kunitconfig_path, KUNITCONFIG_PATH)
			if not os.path.exists(kunitconfig_path):
				raise ConfigError(f'Specified kunitconfig ({kunitconfig_path}) does not exist')
		else:
			kunitconfig_path = get_kunitconfig_path(build_dir)
			if not os.path.exists(kunitconfig_path):
				shutil.copyfile(DEFAULT_KUNITCONFIG_PATH, kunitconfig_path)

		self._kconfig = kunit_config.Kconfig()
		self._kconfig.read_from_file(kunitconfig_path)

	def clean(self) -> bool:
		try:
			self._ops.make_mrproper()
		except ConfigError as e:
			logging.error(e)
			return False
		return True

	def validate_config(self, build_dir) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		validated_kconfig = kunit_config.Kconfig()
		validated_kconfig.read_from_file(kconfig_path)
		if not self._kconfig.is_subset_of(validated_kconfig):
			invalid = self._kconfig.entries() - validated_kconfig.entries()
			message = 'Provided Kconfig is not contained in validated .config. Following fields found in kunitconfig, ' \
					  'but not in .config: %s' % (
					', '.join([str(e) for e in invalid])
			)
			logging.error(message)
			return False
		return True

	def build_config(self, build_dir, make_options) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		if build_dir and not os.path.exists(build_dir):
			os.mkdir(build_dir)
		try:
			self._ops.make_arch_qemuconfig(self._kconfig)
			self._kconfig.write_to_file(kconfig_path)
			self._ops.make_olddefconfig(build_dir, make_options)
		except ConfigError as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def build_reconfig(self, build_dir, make_options) -> bool:
		"""Creates a new .config if it is not a subset of the .kunitconfig."""
		kconfig_path = get_kconfig_path(build_dir)
		if os.path.exists(kconfig_path):
			existing_kconfig = kunit_config.Kconfig()
			existing_kconfig.read_from_file(kconfig_path)
			self._ops.make_arch_qemuconfig(self._kconfig)
			if not self._kconfig.is_subset_of(existing_kconfig):
				print('Regenerating .config ...')
				os.remove(kconfig_path)
				return self.build_config(build_dir, make_options)
			else:
				return True
		else:
			print('Generating .config ...')
			return self.build_config(build_dir, make_options)

	def build_kernel(self, alltests, jobs, build_dir, make_options) -> bool:
		try:
			if alltests:
				self._ops.make_allyesconfig(build_dir, make_options)
			self._ops.make_olddefconfig(build_dir, make_options)
			self._ops.make(jobs, build_dir, make_options)
		except (ConfigError, BuildError) as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def run_kernel(self, args=None, build_dir='', filter_glob='', timeout=None) -> Iterator[str]:
		if not args:
			args = []
		args.extend(['mem=1G', 'console=tty', 'kunit_shutdown=halt'])
		if filter_glob:
			args.append('kunit.filter_glob='+filter_glob)
		outfile = get_outfile_path(build_dir)
		self._ops.run(args, timeout, build_dir, outfile)
		subprocess.call(['stty', 'sane'])
		with open(outfile, 'r') as file:
			for line in file:
				yield line

	def signal_handler(self, sig, frame) -> None:
		logging.error('Build interruption occurred. Cleaning console.')
		subprocess.call(['stty', 'sane'])
