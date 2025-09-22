#!/usr/bin/env python3
#
#===- git-clang-format - ClangFormat Git Integration ---------*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#

r"""
clang-format git integration
============================

This file provides a clang-format integration for git. Put it somewhere in your
path and ensure that it is executable. Then, "git clang-format" will invoke
clang-format on the changes in current files or a specific commit.

For further details, run:
git clang-format -h

Requires Python 2.7 or Python 3
"""

from __future__ import absolute_import, division, print_function
import argparse
import collections
import contextlib
import errno
import os
import re
import subprocess
import sys

usage = ('git clang-format [OPTIONS] [<commit>] [<commit>|--staged] '
         '[--] [<file>...]')

desc = '''
If zero or one commits are given, run clang-format on all lines that differ
between the working directory and <commit>, which defaults to HEAD.  Changes are
only applied to the working directory, or in the stage/index.

Examples:
  To format staged changes, i.e everything that's been `git add`ed:
    git clang-format

  To also format everything touched in the most recent commit:
    git clang-format HEAD~1

  If you're on a branch off main, to format everything touched on your branch:
    git clang-format main

If two commits are given (requires --diff), run clang-format on all lines in the
second <commit> that differ from the first <commit>.

The following git-config settings set the default of the corresponding option:
  clangFormat.binary
  clangFormat.commit
  clangFormat.extensions
  clangFormat.style
'''

# Name of the temporary index file in which save the output of clang-format.
# This file is created within the .git directory.
temp_index_basename = 'clang-format-index'


Range = collections.namedtuple('Range', 'start, count')


def main():
  config = load_git_config()

  # In order to keep '--' yet allow options after positionals, we need to
  # check for '--' ourselves.  (Setting nargs='*' throws away the '--', while
  # nargs=argparse.REMAINDER disallows options after positionals.)
  argv = sys.argv[1:]
  try:
    idx = argv.index('--')
  except ValueError:
    dash_dash = []
  else:
    dash_dash = argv[idx:]
    argv = argv[:idx]

  default_extensions = ','.join([
      # From clang/lib/Frontend/FrontendOptions.cpp, all lower case
      'c', 'h',  # C
      'm',  # ObjC
      'mm',  # ObjC++
      'cc', 'cp', 'cpp', 'c++', 'cxx', 'hh', 'hpp', 'hxx', 'inc',  # C++
      'ccm', 'cppm', 'cxxm', 'c++m',  # C++ Modules
      'cu', 'cuh',  # CUDA
      # Other languages that clang-format supports
      'proto', 'protodevel',  # Protocol Buffers
      'java',  # Java
      'js',  # JavaScript
      'ts',  # TypeScript
      'cs',  # C Sharp
      'json',  # Json
      'sv', 'svh', 'v', 'vh', # Verilog
      ])

  p = argparse.ArgumentParser(
    usage=usage, formatter_class=argparse.RawDescriptionHelpFormatter,
    description=desc)
  p.add_argument('--binary',
                 default=config.get('clangformat.binary', 'clang-format'),
                 help='path to clang-format'),
  p.add_argument('--commit',
                 default=config.get('clangformat.commit', 'HEAD'),
                 help='default commit to use if none is specified'),
  p.add_argument('--diff', action='store_true',
                 help='print a diff instead of applying the changes')
  p.add_argument('--diffstat', action='store_true',
                 help='print a diffstat instead of applying the changes')
  p.add_argument('--extensions',
                 default=config.get('clangformat.extensions',
                                    default_extensions),
                 help=('comma-separated list of file extensions to format, '
                       'excluding the period and case-insensitive')),
  p.add_argument('-f', '--force', action='store_true',
                 help='allow changes to unstaged files')
  p.add_argument('-p', '--patch', action='store_true',
                 help='select hunks interactively')
  p.add_argument('-q', '--quiet', action='count', default=0,
                 help='print less information')
  p.add_argument('--staged', '--cached', action='store_true',
                 help='format lines in the stage instead of the working dir')
  p.add_argument('--style',
                 default=config.get('clangformat.style', None),
                 help='passed to clang-format'),
  p.add_argument('-v', '--verbose', action='count', default=0,
                 help='print extra information')
  p.add_argument('--diff_from_common_commit', action='store_true',
                 help=('diff from the last common commit for commits in '
                      'separate branches rather than the exact point of the '
                      'commits'))
  # We gather all the remaining positional arguments into 'args' since we need
  # to use some heuristics to determine whether or not <commit> was present.
  # However, to print pretty messages, we make use of metavar and help.
  p.add_argument('args', nargs='*', metavar='<commit>',
                 help='revision from which to compute the diff')
  p.add_argument('ignored', nargs='*', metavar='<file>...',
                 help='if specified, only consider differences in these files')
  opts = p.parse_args(argv)

  opts.verbose -= opts.quiet
  del opts.quiet

  commits, files = interpret_args(opts.args, dash_dash, opts.commit)
  if len(commits) > 2:
    die('at most two commits allowed; %d given' % len(commits))
  if len(commits) == 2:
    if opts.staged:
      die('--staged is not allowed when two commits are given')
    if not opts.diff:
      die('--diff is required when two commits are given')
  elif opts.diff_from_common_commit:
    die('--diff_from_common_commit is only allowed when two commits are given')

  if os.path.dirname(opts.binary):
    opts.binary = os.path.abspath(opts.binary)

  changed_lines = compute_diff_and_extract_lines(commits,
                                                 files,
                                                 opts.staged,
                                                 opts.diff_from_common_commit)
  if opts.verbose >= 1:
    ignored_files = set(changed_lines)
  filter_by_extension(changed_lines, opts.extensions.lower().split(','))
  # The computed diff outputs absolute paths, so we must cd before accessing
  # those files.
  cd_to_toplevel()
  filter_symlinks(changed_lines)
  filter_ignored_files(changed_lines, binary=opts.binary)
  if opts.verbose >= 1:
    ignored_files.difference_update(changed_lines)
    if ignored_files:
      print('Ignoring the following files (wrong extension, symlink, or '
            'ignored by clang-format):')
      for filename in ignored_files:
        print('    %s' % filename)
    if changed_lines:
      print('Running clang-format on the following files:')
      for filename in changed_lines:
        print('    %s' % filename)

  if not changed_lines:
    if opts.verbose >= 0:
      print('no modified files to format')
    return 0

  if len(commits) > 1:
    old_tree = commits[1]
    revision = old_tree
  elif opts.staged:
    old_tree = create_tree_from_index(changed_lines)
    revision = ''
  else:
    old_tree = create_tree_from_workdir(changed_lines)
    revision = None
  new_tree = run_clang_format_and_save_to_tree(changed_lines,
                                               revision,
                                               binary=opts.binary,
                                               style=opts.style)
  if opts.verbose >= 1:
    print('old tree: %s' % old_tree)
    print('new tree: %s' % new_tree)

  if old_tree == new_tree:
    if opts.verbose >= 0:
      print('clang-format did not modify any files')
    return 0

  if opts.diff:
    return print_diff(old_tree, new_tree)
  if opts.diffstat:
    return print_diffstat(old_tree, new_tree)

  changed_files = apply_changes(old_tree, new_tree, force=opts.force,
                                patch_mode=opts.patch)
  if (opts.verbose >= 0 and not opts.patch) or opts.verbose >= 1:
    print('changed files:')
    for filename in changed_files:
      print('    %s' % filename)

  return 1


def load_git_config(non_string_options=None):
  """Return the git configuration as a dictionary.

  All options are assumed to be strings unless in `non_string_options`, in which
  is a dictionary mapping option name (in lower case) to either "--bool" or
  "--int"."""
  if non_string_options is None:
    non_string_options = {}
  out = {}
  for entry in run('git', 'config', '--list', '--null').split('\0'):
    if entry:
      if '\n' in entry:
        name, value = entry.split('\n', 1)
      else:
        # A setting with no '=' ('\n' with --null) is implicitly 'true'
        name = entry
        value = 'true'
      if name in non_string_options:
        value = run('git', 'config', non_string_options[name], name)
      out[name] = value
  return out


def interpret_args(args, dash_dash, default_commit):
  """Interpret `args` as "[commits] [--] [files]" and return (commits, files).

  It is assumed that "--" and everything that follows has been removed from
  args and placed in `dash_dash`.

  If "--" is present (i.e., `dash_dash` is non-empty), the arguments to its
  left (if present) are taken as commits.  Otherwise, the arguments are checked
  from left to right if they are commits or files.  If commits are not given,
  a list with `default_commit` is used."""
  if dash_dash:
    if len(args) == 0:
      commits = [default_commit]
    else:
      commits = args
    for commit in commits:
      object_type = get_object_type(commit)
      if object_type not in ('commit', 'tag'):
        if object_type is None:
          die("'%s' is not a commit" % commit)
        else:
          die("'%s' is a %s, but a commit was expected" % (commit, object_type))
    files = dash_dash[1:]
  elif args:
    commits = []
    while args:
      if not disambiguate_revision(args[0]):
        break
      commits.append(args.pop(0))
    if not commits:
      commits = [default_commit]
    files = args
  else:
    commits = [default_commit]
    files = []
  return commits, files


def disambiguate_revision(value):
  """Returns True if `value` is a revision, False if it is a file, or dies."""
  # If `value` is ambiguous (neither a commit nor a file), the following
  # command will die with an appropriate error message.
  run('git', 'rev-parse', value, verbose=False)
  object_type = get_object_type(value)
  if object_type is None:
    return False
  if object_type in ('commit', 'tag'):
    return True
  die('`%s` is a %s, but a commit or filename was expected' %
      (value, object_type))


def get_object_type(value):
  """Returns a string description of an object's type, or None if it is not
  a valid git object."""
  cmd = ['git', 'cat-file', '-t', value]
  p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  stdout, stderr = p.communicate()
  if p.returncode != 0:
    return None
  return convert_string(stdout.strip())


def compute_diff_and_extract_lines(commits, files, staged, diff_common_commit):
  """Calls compute_diff() followed by extract_lines()."""
  diff_process = compute_diff(commits, files, staged, diff_common_commit)
  changed_lines = extract_lines(diff_process.stdout)
  diff_process.stdout.close()
  diff_process.wait()
  if diff_process.returncode != 0:
    # Assume error was already printed to stderr.
    sys.exit(2)
  return changed_lines


def compute_diff(commits, files, staged, diff_common_commit):
  """Return a subprocess object producing the diff from `commits`.

  The return value's `stdin` file object will produce a patch with the
  differences between the working directory (or stage if --staged is used) and
  the first commit if a single one was specified, or the difference between
  both specified commits, filtered on `files` (if non-empty).
  Zero context lines are used in the patch."""
  git_tool = 'diff-index'
  extra_args = []
  if len(commits) == 2:
    git_tool = 'diff-tree'
    if diff_common_commit:
      commits = [f'{commits[0]}...{commits[1]}']
  elif staged:
    extra_args += ['--cached']

  cmd = ['git', git_tool, '-p', '-U0'] + extra_args + commits + ['--']
  cmd.extend(files)
  p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  p.stdin.close()
  return p


def extract_lines(patch_file):
  """Extract the changed lines in `patch_file`.

  The return value is a dictionary mapping filename to a list of (start_line,
  line_count) pairs.

  The input must have been produced with ``-U0``, meaning unidiff format with
  zero lines of context.  The return value is a dict mapping filename to a
  list of line `Range`s."""
  matches = {}
  for line in patch_file:
    line = convert_string(line)
    match = re.search(r'^\+\+\+\ [^/]+/(.*)', line)
    if match:
      filename = match.group(1).rstrip('\r\n\t')
    match = re.search(r'^@@ -[0-9,]+ \+(\d+)(,(\d+))?', line)
    if match:
      start_line = int(match.group(1))
      line_count = 1
      if match.group(3):
        line_count = int(match.group(3))
      if line_count == 0:
        line_count = 1
      if start_line == 0:
        continue
      matches.setdefault(filename, []).append(Range(start_line, line_count))
  return matches


def filter_by_extension(dictionary, allowed_extensions):
  """Delete every key in `dictionary` that doesn't have an allowed extension.

  `allowed_extensions` must be a collection of lowercase file extensions,
  excluding the period."""
  allowed_extensions = frozenset(allowed_extensions)
  for filename in list(dictionary.keys()):
    base_ext = filename.rsplit('.', 1)
    if len(base_ext) == 1 and '' in allowed_extensions:
        continue
    if len(base_ext) == 1 or base_ext[1].lower() not in allowed_extensions:
      del dictionary[filename]


def filter_symlinks(dictionary):
  """Delete every key in `dictionary` that is a symlink."""
  for filename in list(dictionary.keys()):
    if os.path.islink(filename):
      del dictionary[filename]


def filter_ignored_files(dictionary, binary):
  """Delete every key in `dictionary` that is ignored by clang-format."""
  ignored_files = run(binary, '-list-ignored', *dictionary.keys())
  if not ignored_files:
    return
  ignored_files = ignored_files.split('\n')
  for filename in ignored_files:
    del dictionary[filename]


def cd_to_toplevel():
  """Change to the top level of the git repository."""
  toplevel = run('git', 'rev-parse', '--show-toplevel')
  os.chdir(toplevel)


def create_tree_from_workdir(filenames):
  """Create a new git tree with the given files from the working directory.

  Returns the object ID (SHA-1) of the created tree."""
  return create_tree(filenames, '--stdin')


def create_tree_from_index(filenames):
  # Copy the environment, because the files have to be read from the original
  # index.
  env = os.environ.copy()
  def index_contents_generator():
    for filename in filenames:
      git_ls_files_cmd = ['git', 'ls-files', '--stage', '-z', '--', filename]
      git_ls_files = subprocess.Popen(git_ls_files_cmd, env=env,
                                      stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE)
      stdout = git_ls_files.communicate()[0]
      yield convert_string(stdout.split(b'\0')[0])
  return create_tree(index_contents_generator(), '--index-info')


def run_clang_format_and_save_to_tree(changed_lines, revision=None,
                                      binary='clang-format', style=None):
  """Run clang-format on each file and save the result to a git tree.

  Returns the object ID (SHA-1) of the created tree."""
  # Copy the environment when formatting the files in the index, because the
  # files have to be read from the original index.
  env = os.environ.copy() if revision == '' else None
  def iteritems(container):
      try:
          return container.iteritems() # Python 2
      except AttributeError:
          return container.items() # Python 3
  def index_info_generator():
    for filename, line_ranges in iteritems(changed_lines):
      if revision is not None:
        if len(revision) > 0:
          git_metadata_cmd = ['git', 'ls-tree',
                              '%s:%s' % (revision, os.path.dirname(filename)),
                              os.path.basename(filename)]
        else:
          git_metadata_cmd = ['git', 'ls-files', '--stage', '--', filename]
        git_metadata = subprocess.Popen(git_metadata_cmd, env=env,
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE)
        stdout = git_metadata.communicate()[0]
        mode = oct(int(stdout.split()[0], 8))
      else:
        mode = oct(os.stat(filename).st_mode)
      # Adjust python3 octal format so that it matches what git expects
      if mode.startswith('0o'):
          mode = '0' + mode[2:]
      blob_id = clang_format_to_blob(filename, line_ranges,
                                     revision=revision,
                                     binary=binary,
                                     style=style,
                                     env=env)
      yield '%s %s\t%s' % (mode, blob_id, filename)
  return create_tree(index_info_generator(), '--index-info')


def create_tree(input_lines, mode):
  """Create a tree object from the given input.

  If mode is '--stdin', it must be a list of filenames.  If mode is
  '--index-info' is must be a list of values suitable for "git update-index
  --index-info", such as "<mode> <SP> <sha1> <TAB> <filename>".  Any other mode
  is invalid."""
  assert mode in ('--stdin', '--index-info')
  cmd = ['git', 'update-index', '--add', '-z', mode]
  with temporary_index_file():
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for line in input_lines:
      p.stdin.write(to_bytes('%s\0' % line))
    p.stdin.close()
    if p.wait() != 0:
      die('`%s` failed' % ' '.join(cmd))
    tree_id = run('git', 'write-tree')
    return tree_id


def clang_format_to_blob(filename, line_ranges, revision=None,
                         binary='clang-format', style=None, env=None):
  """Run clang-format on the given file and save the result to a git blob.

  Runs on the file in `revision` if not None, or on the file in the working
  directory if `revision` is None. Revision can be set to an empty string to run
  clang-format on the file in the index.

  Returns the object ID (SHA-1) of the created blob."""
  clang_format_cmd = [binary]
  if style:
    clang_format_cmd.extend(['-style='+style])
  clang_format_cmd.extend([
      '-lines=%s:%s' % (start_line, start_line+line_count-1)
      for start_line, line_count in line_ranges])
  if revision is not None:
    clang_format_cmd.extend(['-assume-filename='+filename])
    git_show_cmd = ['git', 'cat-file', 'blob', '%s:%s' % (revision, filename)]
    git_show = subprocess.Popen(git_show_cmd, env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE)
    git_show.stdin.close()
    clang_format_stdin = git_show.stdout
  else:
    clang_format_cmd.extend([filename])
    git_show = None
    clang_format_stdin = subprocess.PIPE
  try:
    clang_format = subprocess.Popen(clang_format_cmd, stdin=clang_format_stdin,
                                    stdout=subprocess.PIPE)
    if clang_format_stdin == subprocess.PIPE:
      clang_format_stdin = clang_format.stdin
  except OSError as e:
    if e.errno == errno.ENOENT:
      die('cannot find executable "%s"' % binary)
    else:
      raise
  clang_format_stdin.close()
  hash_object_cmd = ['git', 'hash-object', '-w', '--path='+filename, '--stdin']
  hash_object = subprocess.Popen(hash_object_cmd, stdin=clang_format.stdout,
                                 stdout=subprocess.PIPE)
  clang_format.stdout.close()
  stdout = hash_object.communicate()[0]
  if hash_object.returncode != 0:
    die('`%s` failed' % ' '.join(hash_object_cmd))
  if clang_format.wait() != 0:
    die('`%s` failed' % ' '.join(clang_format_cmd))
  if git_show and git_show.wait() != 0:
    die('`%s` failed' % ' '.join(git_show_cmd))
  return convert_string(stdout).rstrip('\r\n')


@contextlib.contextmanager
def temporary_index_file(tree=None):
  """Context manager for setting GIT_INDEX_FILE to a temporary file and deleting
  the file afterward."""
  index_path = create_temporary_index(tree)
  old_index_path = os.environ.get('GIT_INDEX_FILE')
  os.environ['GIT_INDEX_FILE'] = index_path
  try:
    yield
  finally:
    if old_index_path is None:
      del os.environ['GIT_INDEX_FILE']
    else:
      os.environ['GIT_INDEX_FILE'] = old_index_path
    os.remove(index_path)


def create_temporary_index(tree=None):
  """Create a temporary index file and return the created file's path.

  If `tree` is not None, use that as the tree to read in.  Otherwise, an
  empty index is created."""
  gitdir = run('git', 'rev-parse', '--git-dir')
  path = os.path.join(gitdir, temp_index_basename)
  if tree is None:
    tree = '--empty'
  run('git', 'read-tree', '--index-output='+path, tree)
  return path


def print_diff(old_tree, new_tree):
  """Print the diff between the two trees to stdout."""
  # We use the porcelain 'diff' and not plumbing 'diff-tree' because the output
  # is expected to be viewed by the user, and only the former does nice things
  # like color and pagination.
  #
  # We also only print modified files since `new_tree` only contains the files
  # that were modified, so unmodified files would show as deleted without the
  # filter.
  return subprocess.run(['git', 'diff', '--diff-filter=M',
                         '--exit-code', old_tree, new_tree]).returncode

def print_diffstat(old_tree, new_tree):
  """Print the diffstat between the two trees to stdout."""
  # We use the porcelain 'diff' and not plumbing 'diff-tree' because the output
  # is expected to be viewed by the user, and only the former does nice things
  # like color and pagination.
  #
  # We also only print modified files since `new_tree` only contains the files
  # that were modified, so unmodified files would show as deleted without the
  # filter.
  return subprocess.run(['git', 'diff', '--diff-filter=M', '--exit-code',
                         '--stat', old_tree, new_tree]).returncode

def apply_changes(old_tree, new_tree, force=False, patch_mode=False):
  """Apply the changes in `new_tree` to the working directory.

  Bails if there are local changes in those files and not `force`.  If
  `patch_mode`, runs `git checkout --patch` to select hunks interactively."""
  changed_files = run('git', 'diff-tree', '--diff-filter=M', '-r', '-z',
                      '--name-only', old_tree,
                      new_tree).rstrip('\0').split('\0')
  if not force:
    unstaged_files = run('git', 'diff-files', '--name-status', *changed_files)
    if unstaged_files:
      print('The following files would be modified but '
                'have unstaged changes:', file=sys.stderr)
      print(unstaged_files, file=sys.stderr)
      print('Please commit, stage, or stash them first.', file=sys.stderr)
      sys.exit(2)
  if patch_mode:
    # In patch mode, we could just as well create an index from the new tree
    # and checkout from that, but then the user will be presented with a
    # message saying "Discard ... from worktree".  Instead, we use the old
    # tree as the index and checkout from new_tree, which gives the slightly
    # better message, "Apply ... to index and worktree".  This is not quite
    # right, since it won't be applied to the user's index, but oh well.
    with temporary_index_file(old_tree):
      subprocess.run(['git', 'checkout', '--patch', new_tree], check=True)
    index_tree = old_tree
  else:
    with temporary_index_file(new_tree):
      run('git', 'checkout-index', '-f', '--', *changed_files)
  return changed_files


def run(*args, **kwargs):
  stdin = kwargs.pop('stdin', '')
  verbose = kwargs.pop('verbose', True)
  strip = kwargs.pop('strip', True)
  for name in kwargs:
    raise TypeError("run() got an unexpected keyword argument '%s'" % name)
  p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       stdin=subprocess.PIPE)
  stdout, stderr = p.communicate(input=stdin)

  stdout = convert_string(stdout)
  stderr = convert_string(stderr)

  if p.returncode == 0:
    if stderr:
      if verbose:
        print('`%s` printed to stderr:' % ' '.join(args), file=sys.stderr)
      print(stderr.rstrip(), file=sys.stderr)
    if strip:
      stdout = stdout.rstrip('\r\n')
    return stdout
  if verbose:
    print('`%s` returned %s' % (' '.join(args), p.returncode), file=sys.stderr)
  if stderr:
    print(stderr.rstrip(), file=sys.stderr)
  sys.exit(2)


def die(message):
  print('error:', message, file=sys.stderr)
  sys.exit(2)


def to_bytes(str_input):
    # Encode to UTF-8 to get binary data.
    if isinstance(str_input, bytes):
        return str_input
    return str_input.encode('utf-8')


def to_string(bytes_input):
    if isinstance(bytes_input, str):
        return bytes_input
    return bytes_input.encode('utf-8')


def convert_string(bytes_input):
    try:
        return to_string(bytes_input.decode('utf-8'))
    except AttributeError: # 'str' object has no attribute 'decode'.
        return str(bytes_input)
    except UnicodeError:
        return str(bytes_input)

if __name__ == '__main__':
  sys.exit(main())
