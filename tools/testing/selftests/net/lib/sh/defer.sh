#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# map[(scope_id,track,cleanup_id) -> cleanup_command]
# track={d=default | p=priority}
declare -A __DEFER__JOBS

# map[(scope_id,track) -> # cleanup_commands]
declare -A __DEFER__NJOBS

# scope_id of the topmost scope.
__DEFER__SCOPE_ID=0

__defer__ndefer_key()
{
	local track=$1; shift

	echo $__DEFER__SCOPE_ID,$track
}

__defer__defer_key()
{
	local track=$1; shift
	local defer_ix=$1; shift

	echo $__DEFER__SCOPE_ID,$track,$defer_ix
}

__defer__ndefers()
{
	local track=$1; shift

	echo ${__DEFER__NJOBS[$(__defer__ndefer_key $track)]}
}

__defer__run()
{
	local track=$1; shift
	local defer_ix=$1; shift
	local defer_key=$(__defer__defer_key $track $defer_ix)

	${__DEFER__JOBS[$defer_key]}
	unset __DEFER__JOBS[$defer_key]
}

__defer__schedule()
{
	local track=$1; shift
	local ndefers=$(__defer__ndefers $track)
	local ndefers_key=$(__defer__ndefer_key $track)
	local defer_key=$(__defer__defer_key $track $ndefers)
	local defer="$@"

	__DEFER__JOBS[$defer_key]="$defer"
	__DEFER__NJOBS[$ndefers_key]=$((ndefers + 1))
}

__defer__scope_wipe()
{
	__DEFER__NJOBS[$(__defer__ndefer_key d)]=0
	__DEFER__NJOBS[$(__defer__ndefer_key p)]=0
}

defer_scope_push()
{
	((__DEFER__SCOPE_ID++))
	__defer__scope_wipe
}

defer_scope_pop()
{
	local defer_ix

	for ((defer_ix=$(__defer__ndefers p); defer_ix-->0; )); do
		__defer__run p $defer_ix
	done

	for ((defer_ix=$(__defer__ndefers d); defer_ix-->0; )); do
		__defer__run d $defer_ix
	done

	__defer__scope_wipe
	((__DEFER__SCOPE_ID--))
}

defer()
{
	__defer__schedule d "$@"
}

defer_prio()
{
	__defer__schedule p "$@"
}

defer_scopes_cleanup()
{
	while ((__DEFER__SCOPE_ID >= 0)); do
		defer_scope_pop
	done
}

in_defer_scope()
{
	local ret

	defer_scope_push
	"$@"
	ret=$?
	defer_scope_pop

	return $ret
}

__defer__scope_wipe
