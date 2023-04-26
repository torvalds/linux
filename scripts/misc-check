#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -e

# Detect files that are tracked but ignored by git. This is checked only when
# ${KBUILD_EXTRA_WARN} contains 1, git is installed, and the source tree is
# tracked by git.
check_tracked_ignored_files () {
	case "${KBUILD_EXTRA_WARN}" in
	*1*) ;;
	*) return;;
	esac

	git -C ${srctree:-.} ls-files -i -c --exclude-per-directory=.gitignore 2>/dev/null |
		sed 's/$/: warning: ignored by one of the .gitignore files/' >&2
}

check_tracked_ignored_files
