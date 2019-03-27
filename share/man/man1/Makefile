#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD$

.include <src.opts.mk>

MAN=	builtin.1 intro.1

PACKAGE=runtime-manuals

.if ${MK_TESTS} != "no"
ATF=	${SRCTOP}/contrib/atf
.PATH:	${ATF}/doc
MAN+=	atf-test-program.1
.endif

# Create MLINKS for Shell built in commands for which there are no userland
# utilities of the same name:
MLINKS=	builtin.1 alias.1 \
	builtin.1 alloc.1 \
	builtin.1 bg.1 \
	builtin.1 bind.1 \
	builtin.1 bindkey.1 \
	builtin.1 break.1 \
	builtin.1 breaksw.1 \
	builtin.1 builtins.1 \
	builtin.1 case.1 \
	builtin.1 cd.1 \
	builtin.1 chdir.1 \
	builtin.1 command.1 \
	builtin.1 complete.1 \
	builtin.1 continue.1 \
	builtin.1 default.1 \
	builtin.1 dirs.1 \
	builtin.1 do.1 \
	builtin.1 done.1 \
	builtin.1 echotc.1 \
	builtin.1 elif.1 \
	builtin.1 else.1 \
	builtin.1 end.1 \
	builtin.1 endif.1 \
	builtin.1 endsw.1 \
	builtin.1 esac.1 \
	builtin.1 eval.1 \
	builtin.1 exec.1 \
	builtin.1 exit.1 \
	builtin.1 export.1 \
	builtin.1 fc.1 \
	builtin.1 fg.1 \
	builtin.1 fi.1 \
	builtin.1 filetest.1 \
	builtin.1 for.1 \
	builtin.1 foreach.1 \
	builtin.1 getopts.1 \
	builtin.1 glob.1 \
	builtin.1 goto.1 \
	builtin.1 hash.1 \
	builtin.1 hashstat.1 \
	builtin.1 history.1 \
	builtin.1 hup.1 \
	builtin.1 if.1 \
	builtin.1 jobid.1 \
	builtin.1 jobs.1 \
	builtin.1 limit.1 \
	builtin.1 log.1 \
	builtin.1 logout.1 \
	builtin.1 ls-F.1 \
	builtin.1 notify.1 \
	builtin.1 onintr.1 \
	builtin.1 popd.1 \
	builtin.1 pushd.1 \
	builtin.1 read.1 \
	builtin.1 readonly.1 \
	builtin.1 rehash.1 \
	builtin.1 repeat.1 \
	builtin.1 sched.1 \
	builtin.1 set.1 \
	builtin.1 setenv.1 \
	builtin.1 settc.1 \
	builtin.1 setty.1 \
	builtin.1 setvar.1 \
	builtin.1 shift.1 \
	builtin.1 source.1 \
	builtin.1 stop.1 \
	builtin.1 suspend.1 \
	builtin.1 switch.1 \
	builtin.1 telltc.1 \
	builtin.1 then.1 \
	builtin.1 times.1 \
	builtin.1 trap.1 \
	builtin.1 type.1 \
	builtin.1 ulimit.1 \
	builtin.1 umask.1 \
	builtin.1 unalias.1 \
	builtin.1 uncomplete.1 \
	builtin.1 unhash.1 \
	builtin.1 unlimit.1 \
	builtin.1 unset.1 \
	builtin.1 unsetenv.1 \
	builtin.1 until.1 \
	builtin.1 wait.1 \
	builtin.1 where.1 \
	builtin.1 while.1
MLINKS+=intro.1 introduction.1

.include <bsd.prog.mk>
