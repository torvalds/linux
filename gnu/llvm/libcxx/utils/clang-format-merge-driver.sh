#!/usr/bin/env bash

# This script can be installed in .git/config to allow rebasing old patches across
# libc++'s clang-format of the whole tree. Most contributors should not require that
# since they don't have many pre-clang-format patches lying around. This script is to
# make it easier for contributors that do have such patches.
#
# The script is installed by running the following from the root of your repository:
#
#   $ git config merge.libcxx-reformat.name "Run clang-format when rebasing libc++ patches"
#   $ git config merge.libcxx-reformat.driver "libcxx/utils/clang-format-merge-driver.sh %O %A %B %P"
#
# This is based on https://github.com/nico/hack/blob/main/notes/auto_git_rebase_across_mechanical_changes.md.
# Many thanks to Nico Weber for paving the way here.

# Path to the file's contents at the ancestor's version.
base="$1"

# Path to the file's contents at the current version.
current="$2"

# Path to the file's contents at the other branch's version (for nonlinear histories, there might be multiple other branches).
other="$3"

# The path of the file in the repository.
path="$4"

clang-format --style=file --assume-filename="$path" < "$base" > "$base.tmp"
mv "$base.tmp" "$base"

clang-format --style=file --assume-filename="$path" < "$current" > "$current.tmp"
mv "$current.tmp" "$current"

clang-format --style=file --assume-filename="$path" < "$other" > "$other.tmp"
mv "$other.tmp" "$other"

git merge-file -Lcurrent -Lbase -Lother "$current" "$base" "$other"
