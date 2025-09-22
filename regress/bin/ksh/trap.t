#	$OpenBSD: trap.t,v 1.6 2022/10/16 10:44:06 kn Exp $

#
# Check that I/O redirection failure triggers the ERR trap.
# stderr patterns are minimal to match all of bash, ksh and ksh93.
# Try writing the root directory to guarantee EISDIR.
#

name: failed-redirect-triggers-ERR-restricted
description:
	Check that restricted mode prevents valid redirections that may write.
arguments: !-r!
stdin:
	trap 'echo ERR' ERR
	true >/dev/null
expected-stdout:
	ERR
expected-stderr-pattern:
	/restricted/
expected-exit: e != 0
---


name: failed-redirect-triggers-ERR-command
description:
	Redirect standard output for a single command.
stdin:
	trap 'echo ERR' ERR
	true >/
expected-stdout:
	ERR
expected-stderr-pattern:
	/Is a directory/
expected-exit: e != 0
---


name: failed-redirect-triggers-ERR-permanent
description:
	Permanently redirect standard output of the shell without execution.
stdin:
	trap 'echo ERR' ERR
	exec >/
expected-stdout:
	ERR
expected-stderr-pattern:
	/Is a directory/
expected-exit: e != 0
---

#
# Check that the errexit option
# a) does not interfere with running traps and
# b) propagates a non-zero exit status from traps.
# Check that traps are run in the same order in which they were triggered.
#

name: failed-ERR-runs-EXIT
# XXX remove once bin/ksh/main.c r1.52 is backed out *AND* a new fix is in
# XXX enable once bin/ksh/main.c r1.52 is backed out
#expected-fail: yes
description:
	Check that EXIT runs under errexit even if ERR failed.
arguments: !-e!
stdin:
	trap 'echo ERR ; false' ERR
	trap 'echo EXIT' EXIT
	false
expected-stdout:
	ERR
	EXIT
expected-exit: e != 0
---


name: errexit-aborts-EXIT
# XXX remove once bin/ksh/main.c r1.52 is backed out
expected-fail: yes
description:
	Check that errexit makes EXIT exit early.
arguments: !-e!
stdin:
	trap 'echo ERR' ERR
	trap 'false ; echo EXIT' EXIT
expected-stdout:
	ERR
expected-exit: e != 0
---


name: EXIT-triggers-ERR
# XXX remove once bin/ksh/main.c r1.52 is backed out
expected-fail: yes
description:
	Check that ERR runs under errexit if EXIT failed.
arguments: !-e!
stdin:
	trap 'echo ERR' ERR
	trap 'echo EXIT ; false' EXIT
	true
expected-stdout:
	EXIT
	ERR
expected-exit: e != 0
---

#
# Check that the errexit option does not interfere with signal handler traps.
#

name: handled-signal-is-no-error
description:
	Check that gracefully handling a signal is not treated as error.
arguments: !-e!
stdin:
	trap 'echo ERR' ERR
	trap 'echo EXIT' EXIT
	trap 'echo USR1' USR1
	kill -USR1 $$
expected-stdout:
	USR1
	EXIT
expected-exit: e == 0
---


name: failed-INTR-runs-EXIT
description:
	Check that EXIT runs under errexit even if interrupt handling failed.
	SIGINT, SIGQUIT, SIGTERM and SIGHUP are handled specially.
	XXX Find/explain the difference if the busy loop runs directly, i.e. not
	inside a subshell or process ($PROG -c "...").
# XXX should always be passed like PROG
arguments: !-e!
env-setup: !ARGS=-e!
stdin:
	exec timeout --preserve-status -s INT -- 0.1s $PROG $ARGS -c '
		trap "echo EXIT" EXIT
		trap "echo INT ; false" INT
		(while : ; do : ; done)
	'
expected-stdout:
	INT
	EXIT
expected-exit: e != 0
---
