#! /bin/sh -
#
# $FreeBSD$

set -e

case $# in
    0)	echo "usage: $0 input-file <config-file>" 1>&2
	exit 1
	;;
esac

if [ -n "$2" -a -f "$2" ]; then
	. $2
fi

sed -e '
s/\$//g
:join
	/\\$/{a\

	N
	s/\\\n//
	b join
	}
2,${
	/^#/!s/\([{}()*,]\)/ \1 /g
}
' < $1 | awk '
	BEGIN {
		printf "#include <sys/param.h>\n"
		printf "#include <machine/atomic.h>\n"
		printf "\n"
		printf "#include <sys/_semaphore.h>\n"
		printf "#include <sys/aio.h>\n"
		printf "#include <sys/cpuset.h>\n"
		printf "#include <sys/jail.h>\n"
		printf "#include <sys/linker.h>\n"
		printf "#include <sys/mac.h>\n"
		printf "#include <sys/module.h>\n"
		printf "#include <sys/mount.h>\n"
		printf "#include <sys/mqueue.h>\n"
		printf "#include <sys/msg.h>\n"
		printf "#include <sys/poll.h>\n"
		printf "#include <sys/proc.h>\n"
		printf "#include <sys/resource.h>\n"
		printf "#include <sys/sem.h>\n"
		printf "#include <sys/shm.h>\n"
		printf "#include <sys/signal.h>\n"
		printf "#include <sys/signalvar.h>\n"
		printf "#include <sys/socket.h>\n"
		printf "#include <sys/stat.h>\n"
		printf "#include <sys/thr.h>\n"
		printf "#include <sys/time.h>\n"
		printf "#include <sys/timex.h>\n"
		printf "#include <sys/timeffc.h>\n"
		printf "#include <sys/ucontext.h>\n"
		printf "#include <sys/utsname.h>\n"
		printf "#include <sys/uuid.h>\n"
		printf "#include <sys/wait.h>\n"
		printf "\n"
		printf "#ifndef _ACL_PRIVATE\n"
		printf "#define _ACL_PRIVATE\n"
		printf "#endif\n"
		printf "#include <sys/acl.h>\n"
		printf "\n"
		printf "#ifndef EBUSY\n"
		printf "#define errno 0\n"
		printf "#define EBUSY 0\n"
		printf "#endif\n"
		printf "#include <sys/umtx.h>\n"
		printf "\n"
		# existing compat shims
		printf "struct ostat;\n"
		printf "struct nstat;\n"
		printf "struct ostatfs;\n"
		printf "struct osigaction;\n"
		printf "struct osigcontext;\n"
		printf "struct oaiocb;\n"
		printf "union semun_old;\n"
		printf "typedef unsigned int osigset_t;\n"
		printf "struct msqid_ds_old;\n"
		printf "struct shmid_ds_old;\n"
		# TODO
		printf "struct ucontext4;\n"
		printf "struct sctp_sndrcvinfo;\n"
		printf "\n"
	}
	NF < 4 || $1 !~ /^[0-9]+$/ {
		next
	}
	$3 ~ "UNIMPL" || $3 ~ "OBSOL" || $3 ~ "NODEF" || $3 ~ "NOPROTO" ||
	$3 ~ "NOSTD"{
		next
	}
	$4 == "{" {
		if ($3 ~ /COMPAT[0-9]*/) {
			n = split($3, flags, /\|/)
			for (i = 1; i <= n; i++) {
				if (flags[i] == "COMPAT") {
					$6 = "o" $6
				} else if (flags[i] ~ /COMPAT[0-9]+/) {
					sub(/COMPAT/, "freebsd", flags[i])
					$6 = flags[i] "_" $6
				}
			}
		}
		$6 = "__sysfake_" $6
		r = ""
		if ($5 != "void")
			r = "0"
		def = ""
		impl = ""
		for ( i = 5; i <= NF; i++) {
			if ($i == ";")
				break;
			if ($i == "," || $i == ")")
				impl = impl " __unused"
			impl = impl " " $i
			def = def " " $i
		}
		printf "%s;\n", def
		printf "%s\n{ return %s; }\n", impl, r
		next
	}
	{
		printf "invalid line: "
		print
	}
'
