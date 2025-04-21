#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# (c) 2025, Sasha Levin <sashal@kernel.org>

usage() {
	echo "Usage: $(basename "$0") [--selftest] [--force] <commit-id> [commit-subject]"
	echo "Resolves a short git commit ID to its full SHA-1 hash, particularly useful for fixing references in commit messages."
	echo ""
	echo "Arguments:"
	echo "  --selftest      Run self-tests"
	echo "  --force         Try to find commit by subject if ID lookup fails"
	echo "  commit-id       Short git commit ID to resolve"
	echo "  commit-subject  Optional commit subject to help resolve between multiple matches"
	exit 1
}

# Convert subject with ellipsis to grep pattern
convert_to_grep_pattern() {
	local subject="$1"
	# First escape ALL regex special characters
	local escaped_subject
	escaped_subject=$(printf '%s\n' "$subject" | sed 's/[[\.*^$()+?{}|]/\\&/g')
	# Also escape colons, parentheses, and hyphens as they are special in our context
	escaped_subject=$(echo "$escaped_subject" | sed 's/[:-]/\\&/g')
	# Then convert escaped ... sequence to .*?
	escaped_subject=$(echo "$escaped_subject" | sed 's/\\\.\\\.\\\./.*?/g')
	echo "^${escaped_subject}$"
}

git_resolve_commit() {
	local force=0
	if [ "$1" = "--force" ]; then
		force=1
		shift
	fi

	# Split input into commit ID and subject
	local input="$*"
	local commit_id="${input%% *}"
	local subject=""

	# Extract subject if present (everything after the first space)
	if [[ "$input" == *" "* ]]; then
		subject="${input#* }"
		# Strip the ("...") quotes if present
		subject="${subject#*(\"}"
		subject="${subject%\")*}"
	fi

	# Get all possible matching commit IDs
	local matches
	readarray -t matches < <(git rev-parse --disambiguate="$commit_id" 2>/dev/null)

	# Return immediately if we have exactly one match
	if [ ${#matches[@]} -eq 1 ]; then
		echo "${matches[0]}"
		return 0
	fi

	# If no matches and not in force mode, return failure
	if [ ${#matches[@]} -eq 0 ] && [ $force -eq 0 ]; then
		return 1
	fi

	# If we have a subject, try to find a match with that subject
	if [ -n "$subject" ]; then
		# Convert subject with possible ellipsis to grep pattern
		local grep_pattern
		grep_pattern=$(convert_to_grep_pattern "$subject")

		# In force mode with no ID matches, use git log --grep directly
		if [ ${#matches[@]} -eq 0 ] && [ $force -eq 1 ]; then
			# Use git log to search, but filter to ensure subject matches exactly
			local match
			match=$(git log --format="%H %s" --grep="$grep_pattern" --perl-regexp -10 | \
					while read -r hash subject; do
						if echo "$subject" | grep -qP "$grep_pattern"; then
							echo "$hash"
							break
						fi
					done)
			if [ -n "$match" ]; then
				echo "$match"
				return 0
			fi
		else
			# Normal subject matching for existing matches
			for match in "${matches[@]}"; do
				if git log -1 --format="%s" "$match" | grep -qP "$grep_pattern"; then
					echo "$match"
					return 0
				fi
			done
		fi
	fi

	# No match found
	return 1
}

run_selftest() {
	local test_cases=(
		'00250b5 ("MAINTAINERS: add new Rockchip SoC list")'
		'0037727 ("KVM: selftests: Convert xen_shinfo_test away from VCPU_ID")'
		'ffef737 ("net/tls: Fix skb memory leak when running kTLS traffic")'
		'd3d7 ("cifs: Improve guard for excluding $LXDEV xattr")'
		'dbef ("Rename .data.once to .data..once to fix resetting WARN*_ONCE")'
		'12345678'  # Non-existent commit
		'12345 ("I'\''m a dummy commit")'  # Valid prefix but wrong subject
		'--force 99999999 ("net/tls: Fix skb memory leak when running kTLS traffic")'  # Force mode with non-existent ID but valid subject
		'83be ("firmware: ... auto-update: fix poll_complete() ... errors")'  # Wildcard test
		'--force 999999999999 ("firmware: ... auto-update: fix poll_complete() ... errors")'  # Force mode wildcard test
	)

	local expected=(
		"00250b529313d6262bb0ebbd6bdf0a88c809f6f0"
		"0037727b3989c3fe1929c89a9a1dfe289ad86f58"
		"ffef737fd0372ca462b5be3e7a592a8929a82752"
		"d3d797e326533794c3f707ce1761da7a8895458c"
		"dbefa1f31a91670c9e7dac9b559625336206466f"
		""  # Expect empty output for non-existent commit
		""  # Expect empty output for wrong subject
		"ffef737fd0372ca462b5be3e7a592a8929a82752"  # Should find commit by subject in force mode
		"83beece5aff75879bdfc6df8ba84ea88fd93050e"  # Wildcard test
		"83beece5aff75879bdfc6df8ba84ea88fd93050e"  # Force mode wildcard test
	)

	local expected_exit_codes=(
		0
		0
		0
		0
		0
		1  # Expect failure for non-existent commit
		1  # Expect failure for wrong subject
		0  # Should succeed in force mode
		0  # Should succeed with wildcard
		0  # Should succeed with force mode and wildcard
	)

	local failed=0

	echo "Running self-tests..."
	for i in "${!test_cases[@]}"; do
		# Capture both output and exit code
		local result
		result=$(git_resolve_commit ${test_cases[$i]})  # Removed quotes to allow --force to be parsed
		local exit_code=$?

		# Check both output and exit code
		if [ "$result" != "${expected[$i]}" ] || [ $exit_code != ${expected_exit_codes[$i]} ]; then
			echo "Test case $((i+1)) FAILED"
			echo "Input: ${test_cases[$i]}"
			echo "Expected output: '${expected[$i]}'"
			echo "Got output: '$result'"
			echo "Expected exit code: ${expected_exit_codes[$i]}"
			echo "Got exit code: $exit_code"
			failed=1
		else
			echo "Test case $((i+1)) PASSED"
		fi
	done

	if [ $failed -eq 0 ]; then
		echo "All tests passed!"
		exit 0
	else
		echo "Some tests failed!"
		exit 1
	fi
}

# Check for selftest
if [ "$1" = "--selftest" ]; then
	run_selftest
	exit $?
fi

# Handle --force flag
force=""
if [ "$1" = "--force" ]; then
	force="--force"
	shift
fi

# Verify arguments
if [ $# -eq 0 ]; then
	usage
fi

# Skip validation in force mode
if [ -z "$force" ]; then
	# Validate that the first argument matches at least one git commit
	if [ "$(git rev-parse --disambiguate="$1" 2>/dev/null | wc -l)" -eq 0 ]; then
		echo "Error: '$1' does not match any git commit"
		exit 1
	fi
fi

git_resolve_commit $force "$@"
exit $?
