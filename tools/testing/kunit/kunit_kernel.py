# SPDX-License-Identifier: GPL-2.0
#
# Runs UML kernel, collects output, and handles errors.
#
# Copyright (C) 2019, Google LLC.
# Author: Felix Guo <felixguoxiuping@gmail.com>
# Author: Brendan Higgins <brendanhiggins@google.com>

import importlib.abc
import importlib.util
import logging
import subprocess
import os
import shlex
import shutil
import signal
import threading
from typing import Iterator, List, Optional, Tuple
from types import FrameType

import kunit_config
import qemu_config

KCONFIG_PATH = '.config'
KUNITCONFIG_PATH = '.kunitconfig'
OLD_KUNITCONFIG_PATH = 'last_used_kunitconfig'
DEFAULT_KUNITCONFIG_PATH = 'tools/testing/kunit/configs/default.config'
ALL_TESTS_CONFIG_PATH = 'tools/testing/kunit/configs/all_tests.config'
UML_KCONFIG_PATH = 'tools/testing/kunit/configs/arch_uml.config'
OUTFILE_PATH = 'test.log'
ABS_TOOL_PATH = os.path.abspath(os.path.dirname(__file__))
QEMU_CONFIGS_DIR = os.path.join(ABS_TOOL_PATH, 'qemu_configs')

class ConfigError(Exception):
	"""Represents an error trying to configure the Linux kernel."""


class BuildError(Exception):
	"""Represents an error trying to build the Linux kernel."""


class LinuxSourceTreeOperations:
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

	def make_arch_config(self, base_kunitconfig: kunit_config.Kconfig) -> kunit_config.Kconfig:
		return base_kunitconfig

	def make_olddefconfig(self, build_dir: str, make_options: Optional[List[str]]) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, 'O=' + build_dir, 'olddefconfig']
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
		if make_options:
			command.extend(make_options)
		print('Populating config with:\n$', ' '.join(command))
		try:
			subprocess.check_output(command, stderr=subprocess.STDOUT)
		except OSError as e:
			raise ConfigError('Could not call make command: ' + str(e))
		except subprocess.CalledProcessError as e:
			raise ConfigError(e.output.decode())

	def make(self, jobs: int, build_dir: str, make_options: Optional[List[str]]) -> None:
		command = ['make', 'ARCH=' + self._linux_arch, 'O=' + build_dir, '--jobs=' + str(jobs)]
		if make_options:
			command.extend(make_options)
		if self._cross_compile:
			command += ['CROSS_COMPILE=' + self._cross_compile]
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

	def start(self, params: List[str], build_dir: str) -> subprocess.Popen:
		raise RuntimeError('not implemented!')


class LinuxSourceTreeOperationsQemu(LinuxSourceTreeOperations):

	def __init__(self, qemu_arch_params: qemu_config.QemuArchParams, cross_compile: Optional[str]):
		super().__init__(linux_arch=qemu_arch_params.linux_arch,
				 cross_compile=cross_compile)
		self._kconfig = qemu_arch_params.kconfig
		self._qemu_arch = qemu_arch_params.qemu_arch
		self._kernel_path = qemu_arch_params.kernel_path
		self._kernel_command_line = qemu_arch_params.kernel_command_line + ' kunit_shutdown=reboot'
		self._extra_qemu_params = qemu_arch_params.extra_qemu_params
		self._serial = qemu_arch_params.serial

	def make_arch_config(self, base_kunitconfig: kunit_config.Kconfig) -> kunit_config.Kconfig:
		kconfig = kunit_config.parse_from_string(self._kconfig)
		kconfig.merge_in_entries(base_kunitconfig)
		return kconfig

	def start(self, params: List[str], build_dir: str) -> subprocess.Popen:
		kernel_path = os.path.join(build_dir, self._kernel_path)
		qemu_command = ['qemu-system-' + self._qemu_arch,
				'-nodefaults',
				'-m', '1024',
				'-kernel', kernel_path,
				'-append', ' '.join(params + [self._kernel_command_line]),
				'-no-reboot',
				'-nographic',
				'-serial', self._serial] + self._extra_qemu_params
		# Note: shlex.join() does what we want, but requires python 3.8+.
		print('Running tests with:\n$', ' '.join(shlex.quote(arg) for arg in qemu_command))
		return subprocess.Popen(qemu_command,
					stdin=subprocess.PIPE,
					stdout=subprocess.PIPE,
					stderr=subprocess.STDOUT,
					text=True, errors='backslashreplace')

class LinuxSourceTreeOperationsUml(LinuxSourceTreeOperations):
	"""An abstraction over command line operations performed on a source tree."""

	def __init__(self, cross_compile: Optional[str]=None):
		super().__init__(linux_arch='um', cross_compile=cross_compile)

	def make_arch_config(self, base_kunitconfig: kunit_config.Kconfig) -> kunit_config.Kconfig:
		kconfig = kunit_config.parse_file(UML_KCONFIG_PATH)
		kconfig.merge_in_entries(base_kunitconfig)
		return kconfig

	def start(self, params: List[str], build_dir: str) -> subprocess.Popen:
		"""Runs the Linux UML binary. Must be named 'linux'."""
		linux_bin = os.path.join(build_dir, 'linux')
		params.extend(['mem=1G', 'console=tty', 'kunit_shutdown=halt'])
		print('Running tests with:\n$', linux_bin, ' '.join(shlex.quote(arg) for arg in params))
		return subprocess.Popen([linux_bin] + params,
					   stdin=subprocess.PIPE,
					   stdout=subprocess.PIPE,
					   stderr=subprocess.STDOUT,
					   text=True, errors='backslashreplace')

def get_kconfig_path(build_dir: str) -> str:
	return os.path.join(build_dir, KCONFIG_PATH)

def get_kunitconfig_path(build_dir: str) -> str:
	return os.path.join(build_dir, KUNITCONFIG_PATH)

def get_old_kunitconfig_path(build_dir: str) -> str:
	return os.path.join(build_dir, OLD_KUNITCONFIG_PATH)

def get_parsed_kunitconfig(build_dir: str,
			   kunitconfig_paths: Optional[List[str]]=None) -> kunit_config.Kconfig:
	if not kunitconfig_paths:
		path = get_kunitconfig_path(build_dir)
		if not os.path.exists(path):
			shutil.copyfile(DEFAULT_KUNITCONFIG_PATH, path)
		return kunit_config.parse_file(path)

	merged = kunit_config.Kconfig()

	for path in kunitconfig_paths:
		if os.path.isdir(path):
			path = os.path.join(path, KUNITCONFIG_PATH)
		if not os.path.exists(path):
			raise ConfigError(f'Specified kunitconfig ({path}) does not exist')

		partial = kunit_config.parse_file(path)
		diff = merged.conflicting_options(partial)
		if diff:
			diff_str = '\n\n'.join(f'{a}\n  vs from {path}\n{b}' for a, b in diff)
			raise ConfigError(f'Multiple values specified for {len(diff)} options in kunitconfig:\n{diff_str}')
		merged.merge_in_entries(partial)
	return merged

def get_outfile_path(build_dir: str) -> str:
	return os.path.join(build_dir, OUTFILE_PATH)

def _default_qemu_config_path(arch: str) -> str:
	config_path = os.path.join(QEMU_CONFIGS_DIR, arch + '.py')
	if os.path.isfile(config_path):
		return config_path

	options = [f[:-3] for f in os.listdir(QEMU_CONFIGS_DIR) if f.endswith('.py')]
	raise ConfigError(arch + ' is not a valid arch, options are ' + str(sorted(options)))

def _get_qemu_ops(config_path: str,
		  extra_qemu_args: Optional[List[str]],
		  cross_compile: Optional[str]) -> Tuple[str, LinuxSourceTreeOperations]:
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
	assert spec is not None
	config = importlib.util.module_from_spec(spec)
	# See https://github.com/python/typeshed/pull/2626 for context.
	assert isinstance(spec.loader, importlib.abc.Loader)
	spec.loader.exec_module(config)

	if not hasattr(config, 'QEMU_ARCH'):
		raise ValueError('qemu_config module missing "QEMU_ARCH": ' + config_path)
	params: qemu_config.QemuArchParams = config.QEMU_ARCH
	if extra_qemu_args:
		params.extra_qemu_params.extend(extra_qemu_args)
	return params.linux_arch, LinuxSourceTreeOperationsQemu(
			params, cross_compile=cross_compile)

class LinuxSourceTree:
	"""Represents a Linux kernel source tree with KUnit tests."""

	def __init__(
	      self,
	      build_dir: str,
	      kunitconfig_paths: Optional[List[str]]=None,
	      kconfig_add: Optional[List[str]]=None,
	      arch: Optional[str]=None,
	      cross_compile: Optional[str]=None,
	      qemu_config_path: Optional[str]=None,
	      extra_qemu_args: Optional[List[str]]=None) -> None:
		signal.signal(signal.SIGINT, self.signal_handler)
		if qemu_config_path:
			self._arch, self._ops = _get_qemu_ops(qemu_config_path, extra_qemu_args, cross_compile)
		else:
			self._arch = 'um' if arch is None else arch
			if self._arch == 'um':
				self._ops = LinuxSourceTreeOperationsUml(cross_compile=cross_compile)
			else:
				qemu_config_path = _default_qemu_config_path(self._arch)
				_, self._ops = _get_qemu_ops(qemu_config_path, extra_qemu_args, cross_compile)

		self._kconfig = get_parsed_kunitconfig(build_dir, kunitconfig_paths)
		if kconfig_add:
			kconfig = kunit_config.parse_from_string('\n'.join(kconfig_add))
			self._kconfig.merge_in_entries(kconfig)

	def arch(self) -> str:
		return self._arch

	def clean(self) -> bool:
		try:
			self._ops.make_mrproper()
		except ConfigError as e:
			logging.error(e)
			return False
		return True

	def validate_config(self, build_dir: str) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		validated_kconfig = kunit_config.parse_file(kconfig_path)
		if self._kconfig.is_subset_of(validated_kconfig):
			return True
		missing = set(self._kconfig.as_entries()) - set(validated_kconfig.as_entries())
		message = 'Not all Kconfig options selected in kunitconfig were in the generated .config.\n' \
			  'This is probably due to unsatisfied dependencies.\n' \
			  'Missing: ' + ', '.join(str(e) for e in missing)
		if self._arch == 'um':
			message += '\nNote: many Kconfig options aren\'t available on UML. You can try running ' \
				   'on a different architecture with something like "--arch=x86_64".'
		logging.error(message)
		return False

	def build_config(self, build_dir: str, make_options: Optional[List[str]]) -> bool:
		kconfig_path = get_kconfig_path(build_dir)
		if build_dir and not os.path.exists(build_dir):
			os.mkdir(build_dir)
		try:
			self._kconfig = self._ops.make_arch_config(self._kconfig)
			self._kconfig.write_to_file(kconfig_path)
			self._ops.make_olddefconfig(build_dir, make_options)
		except ConfigError as e:
			logging.error(e)
			return False
		if not self.validate_config(build_dir):
			return False

		old_path = get_old_kunitconfig_path(build_dir)
		if os.path.exists(old_path):
			os.remove(old_path)  # write_to_file appends to the file
		self._kconfig.write_to_file(old_path)
		return True

	def _kunitconfig_changed(self, build_dir: str) -> bool:
		old_path = get_old_kunitconfig_path(build_dir)
		if not os.path.exists(old_path):
			return True

		old_kconfig = kunit_config.parse_file(old_path)
		return old_kconfig != self._kconfig

	def build_reconfig(self, build_dir: str, make_options: Optional[List[str]]) -> bool:
		"""Creates a new .config if it is not a subset of the .kunitconfig."""
		kconfig_path = get_kconfig_path(build_dir)
		if not os.path.exists(kconfig_path):
			print('Generating .config ...')
			return self.build_config(build_dir, make_options)

		existing_kconfig = kunit_config.parse_file(kconfig_path)
		self._kconfig = self._ops.make_arch_config(self._kconfig)

		if self._kconfig.is_subset_of(existing_kconfig) and not self._kunitconfig_changed(build_dir):
			return True
		print('Regenerating .config ...')
		os.remove(kconfig_path)
		return self.build_config(build_dir, make_options)

	def build_kernel(self, jobs: int, build_dir: str, make_options: Optional[List[str]]) -> bool:
		try:
			self._ops.make_olddefconfig(build_dir, make_options)
			self._ops.make(jobs, build_dir, make_options)
		except (ConfigError, BuildError) as e:
			logging.error(e)
			return False
		return self.validate_config(build_dir)

	def run_kernel(self, args: Optional[List[str]]=None, build_dir: str='', filter_glob: str='', filter: str='', filter_action: Optional[str]=None, timeout: Optional[int]=None) -> Iterator[str]:
		if not args:
			args = []
		if filter_glob:
			args.append('kunit.filter_glob=' + filter_glob)
		if filter:
			args.append('kunit.filter="' + filter + '"')
		if filter_action:
			args.append('kunit.filter_action=' + filter_action)
		args.append('kunit.enable=1')

		process = self._ops.start(args, build_dir)
		assert process.stdout is not None  # tell mypy it's set

		# Enforce the timeout in a background thread.
		def _wait_proc() -> None:
			try:
				process.wait(timeout=timeout)
			except Exception as e:
				print(e)
				process.terminate()
				process.wait()
		waiter = threading.Thread(target=_wait_proc)
		waiter.start()

		output = open(get_outfile_path(build_dir), 'w')
		try:
			# Tee the output to the file and to our caller in real time.
			for line in process.stdout:
				output.write(line)
				yield line
		# This runs even if our caller doesn't consume every line.
		finally:
			# Flush any leftover output to the file
			output.write(process.stdout.read())
			output.close()
			process.stdout.close()

			waiter.join()
			subprocess.call(['stty', 'sane'])

	def signal_handler(self, unused_sig: int, unused_frame: Optional[FrameType]) -> None:
		logging.error('Build interruption occurred. Cleaning console.')
		subprocess.call(['stty', 'sane'])
