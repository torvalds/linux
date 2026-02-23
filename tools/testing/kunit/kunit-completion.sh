# SPDX-License-Identifier: GPL-2.0
# bash completion support for KUnit

_kunit_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

_kunit()
{
	local cur prev words cword
	_init_completion || return

	local script="${_kunit_dir}/kunit.py"

	if [[ $cword -eq 1 && "$cur" != -* ]]; then
		local cmds=$(${script} --list-cmds 2>/dev/null)
		COMPREPLY=($(compgen -W "${cmds}" -- "$cur"))
		return 0
	fi

	if [[ "$cur" == -* ]]; then
		if [[ -n "${words[1]}" && "${words[1]}" != -* ]]; then
			local opts=$(${script} ${words[1]} --list-opts 2>/dev/null)
			COMPREPLY=($(compgen -W "${opts}" -- "$cur"))
			return 0
		else
			local opts=$(${script} --list-opts 2>/dev/null)
			COMPREPLY=($(compgen -W "${opts}" -- "$cur"))
			return 0
		fi
	fi
}

complete -o default -F _kunit kunit.py
complete -o default -F _kunit kunit
complete -o default -F _kunit ./tools/testing/kunit/kunit.py
