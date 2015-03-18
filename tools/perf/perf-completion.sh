# perf bash and zsh completion

# Taken from git.git's completion script.
__my_reassemble_comp_words_by_ref()
{
	local exclude i j first
	# Which word separators to exclude?
	exclude="${1//[^$COMP_WORDBREAKS]}"
	cword_=$COMP_CWORD
	if [ -z "$exclude" ]; then
		words_=("${COMP_WORDS[@]}")
		return
	fi
	# List of word completion separators has shrunk;
	# re-assemble words to complete.
	for ((i=0, j=0; i < ${#COMP_WORDS[@]}; i++, j++)); do
		# Append each nonempty word consisting of just
		# word separator characters to the current word.
		first=t
		while
			[ $i -gt 0 ] &&
			[ -n "${COMP_WORDS[$i]}" ] &&
			# word consists of excluded word separators
			[ "${COMP_WORDS[$i]//[^$exclude]}" = "${COMP_WORDS[$i]}" ]
		do
			# Attach to the previous token,
			# unless the previous token is the command name.
			if [ $j -ge 2 ] && [ -n "$first" ]; then
				((j--))
			fi
			first=
			words_[$j]=${words_[j]}${COMP_WORDS[i]}
			if [ $i = $COMP_CWORD ]; then
				cword_=$j
			fi
			if (($i < ${#COMP_WORDS[@]} - 1)); then
				((i++))
			else
				# Done.
				return
			fi
		done
		words_[$j]=${words_[j]}${COMP_WORDS[i]}
		if [ $i = $COMP_CWORD ]; then
			cword_=$j
		fi
	done
}

type _get_comp_words_by_ref &>/dev/null ||
_get_comp_words_by_ref()
{
	local exclude cur_ words_ cword_
	if [ "$1" = "-n" ]; then
		exclude=$2
		shift 2
	fi
	__my_reassemble_comp_words_by_ref "$exclude"
	cur_=${words_[cword_]}
	while [ $# -gt 0 ]; do
		case "$1" in
		cur)
			cur=$cur_
			;;
		prev)
			prev=${words_[$cword_-1]}
			;;
		words)
			words=("${words_[@]}")
			;;
		cword)
			cword=$cword_
			;;
		esac
		shift
	done
}

type __ltrim_colon_completions &>/dev/null ||
__ltrim_colon_completions()
{
	if [[ "$1" == *:* && "$COMP_WORDBREAKS" == *:* ]]; then
		# Remove colon-word prefix from COMPREPLY items
		local colon_word=${1%"${1##*:}"}
		local i=${#COMPREPLY[*]}
		while [[ $((--i)) -ge 0 ]]; do
			COMPREPLY[$i]=${COMPREPLY[$i]#"$colon_word"}
		done
	fi
}

__perfcomp ()
{
	COMPREPLY=( $( compgen -W "$1" -- "$2" ) )
}

__perfcomp_colon ()
{
	__perfcomp "$1" "$2"
	__ltrim_colon_completions $cur
}

__perf_prev_skip_opts ()
{
	local i cmd_ cmds_

	let i=cword-1
	cmds_=$($cmd $1 --list-cmds)
	prev_skip_opts=()
	while [ $i -ge 0 ]; do
		if [[ ${words[i]} == $1 ]]; then
			return
		fi
		for cmd_ in $cmds_; do
			if [[ ${words[i]} == $cmd_ ]]; then
				prev_skip_opts=${words[i]}
				return
			fi
		done
		((i--))
	done
}
__perf_main ()
{
	local cmd

	cmd=${words[0]}
	COMPREPLY=()

	# Skip options backward and find the last perf command
	__perf_prev_skip_opts
	# List perf subcommands or long options
	if [ $cword -eq 1 ]; then
		if [[ $cur == --* ]]; then
			cmds=$($cmd --list-opts)
		else
			cmds=$($cmd --list-cmds)
		fi
		__perfcomp "$cmds" "$cur"
	# List possible events for -e option
	elif [[ $prev == "-e" && "${words[1]}" == @(record|stat|top) ]]; then
		evts=$($cmd list --raw-dump)
		__perfcomp_colon "$evts" "$cur"
	else
		# List subcommands for perf commands
		if [[ $prev_skip_opts == @(kvm|kmem|mem|lock|sched) ]]; then
			subcmds=$($cmd $prev_skip_opts --list-cmds)
			__perfcomp_colon "$subcmds" "$cur"
		fi
		# List long option names
		if [[ $cur == --* ]];  then
			subcmd=$prev_skip_opts
			__perf_prev_skip_opts $subcmd
			subcmd=$subcmd" "$prev_skip_opts
			opts=$($cmd $subcmd --list-opts)
			__perfcomp "$opts" "$cur"
		fi
	fi
}

if [[ -n ${ZSH_VERSION-} ]]; then
	autoload -U +X compinit && compinit

	__perfcomp ()
	{
		emulate -L zsh

		local c IFS=$' \t\n'
		local -a array

		for c in ${=1}; do
			case $c in
			--*=*|*.) ;;
			*) c="$c " ;;
			esac
			array[${#array[@]}+1]="$c"
		done

		compset -P '*[=:]'
		compadd -Q -S '' -a -- array && _ret=0
	}

	__perfcomp_colon ()
	{
		emulate -L zsh

		local cur_="${2-$cur}"
		local c IFS=$' \t\n'
		local -a array

		if [[ "$cur_" == *:* ]]; then
			local colon_word=${cur_%"${cur_##*:}"}
		fi

		for c in ${=1}; do
			case $c in
			--*=*|*.) ;;
			*) c="$c " ;;
			esac
			array[$#array+1]=${c#"$colon_word"}
		done

		compset -P '*[=:]'
		compadd -Q -S '' -a -- array && _ret=0
	}

	_perf ()
	{
		local _ret=1 cur cword prev
		cur=${words[CURRENT]}
		prev=${words[CURRENT-1]}
		let cword=CURRENT-1
		emulate ksh -c __perf_main
		let _ret && _default && _ret=0
		return _ret
	}

	compdef _perf perf
	return
fi

type perf &>/dev/null &&
_perf()
{
	local cur words cword prev
	_get_comp_words_by_ref -n =: cur words cword prev
	__perf_main
} &&

complete -o bashdefault -o default -o nospace -F _perf perf 2>/dev/null \
	|| complete -o default -o nospace -F _perf perf
