$ $FreeBSD$
$
$ Message catalog for C locale (template)
$
$ strerror() support catalog
$
$set 1
$ EPERM
1 Operation not permitted
$ ENOENT
2 No such file or directory
$ ESRCH
3 No such process
$ EINTR
4 Interrupted system call
$ EIO
5 Input/output error
$ ENXIO
6 Device not configured
$ E2BIG
7 Argument list too long
$ ENOEXEC
8 Exec format error
$ EBADF
9 Bad file descriptor
$ ECHILD
10 No child processes
$ EDEADLK
11 Resource deadlock avoided
$ ENOMEM
12 Cannot allocate memory
$ EACCES
13 Permission denied
$ EFAULT
14 Bad address
$ ENOTBLK
15 Block device required
$ EBUSY
16 Device busy
$ EEXIST
17 File exists
$ EXDEV
18 Cross-device link
$ ENODEV
19 Operation not supported by device
$ ENOTDIR
20 Not a directory
$ EISDIR
21 Is a directory
$ EINVAL
22 Invalid argument
$ ENFILE
23 Too many open files in system
$ EMFILE
24 Too many open files
$ ENOTTY
25 Inappropriate ioctl for device
$ ETXTBSY
26 Text file busy
$ EFBIG
27 File too large
$ ENOSPC
28 No space left on device
$ ESPIPE
29 Illegal seek
$ EROFS
30 Read-only file system
$ EMLINK
31 Too many links
$ EPIPE
32 Broken pipe
$ EDOM
33 Numerical argument out of domain
$ ERANGE
34 Result too large
$ EAGAIN, EWOULDBLOCK
35 Resource temporarily unavailable
$ EINPROGRESS
36 Operation now in progress
$ EALREADY
37 Operation already in progress
$ ENOTSOCK
38 Socket operation on non-socket
$ EDESTADDRREQ
39 Destination address required
$ EMSGSIZE
40 Message too long
$ EPROTOTYPE
41 Protocol wrong type for socket
$ ENOPROTOOPT
42 Protocol not available
$ EPROTONOSUPPORT
43 Protocol not supported
$ ESOCKTNOSUPPORT
44 Socket type not supported
$ EOPNOTSUPP
45 Operation not supported
$ EPFNOSUPPORT
46 Protocol family not supported
$ EAFNOSUPPORT
47 Address family not supported by protocol family
$ EADDRINUSE
48 Address already in use
$ EADDRNOTAVAIL
49 Can't assign requested address
$ ENETDOWN
50 Network is down
$ ENETUNREACH
51 Network is unreachable
$ ENETRESET
52 Network dropped connection on reset
$ ECONNABORTED
53 Software caused connection abort
$ ECONNRESET
54 Connection reset by peer
$ ENOBUFS
55 No buffer space available
$ EISCONN
56 Socket is already connected
$ ENOTCONN
57 Socket is not connected
$ ESHUTDOWN
58 Can't send after socket shutdown
$ ETOOMANYREFS
59 Too many references: can't splice
$ ETIMEDOUT
60 Operation timed out
$ ECONNREFUSED
61 Connection refused
$ ELOOP
62 Too many levels of symbolic links
$ ENAMETOOLONG
63 File name too long
$ EHOSTDOWN
64 Host is down
$ EHOSTUNREACH
65 No route to host
$ ENOTEMPTY
66 Directory not empty
$ EPROCLIM
67 Too many processes
$ EUSERS
68 Too many users
$ EDQUOT
69 Disc quota exceeded
$ ESTALE
70 Stale NFS file handle
$ EREMOTE
71 Too many levels of remote in path
$ EBADRPC
72 RPC struct is bad
$ ERPCMISMATCH
73 RPC version wrong
$ EPROGUNAVAIL
74 RPC prog. not avail
$ EPROGMISMATCH
75 Program version wrong
$ EPROCUNAVAIL
76 Bad procedure for program
$ ENOLCK
77 No locks available
$ ENOSYS
78 Function not implemented
$ EFTYPE
79 Inappropriate file type or format
$ EAUTH
80 Authentication error
$ ENEEDAUTH
81 Need authenticator
$ EIDRM
82 Identifier removed
$ ENOMSG
83 No message of desired type
$ EOVERFLOW
84 Value too large to be stored in data type
$ ECANCELED
85 Operation canceled
$ EILSEQ
86 Illegal byte sequence
$ ENOATTR
87 Attribute not found
$ EDOOFUS
88 Programming error
$ EBADMSG
89 Bad message
$ EMULTIHOP
90 Multihop attempted
$ ENOLINK
91 Link has been severed
$ EPROTO
92 Protocol error
$ ENOTCAPABLE
93 Capabilities insufficient
$ ECAPMODE
94 Not permitted in capability mode
$ ENOTRECOVERABLE
95 State not recoverable
$ EOWNERDEAD
96 Previous owner died
$ EINTEGRITY
97 Integrity check failed
$
$ strsignal() support catalog
$
$set 2
$ SIGHUP
1 Hangup
$ SIGINT
2 Interrupt
$ SIGQUIT
3 Quit
$ SIGILL
4 Illegal instruction
$ SIGTRAP
5 Trace/BPT trap
$ SIGABRT
6 Abort trap
$ SIGEMT
7 EMT trap
$ SIGFPE
8 Floating point exception
$ SIGKILL
9 Killed
$ SIGBUS
10 Bus error
$ SIGSEGV
11 Segmentation fault
$ SIGSYS
12 Bad system call
$ SIGPIPE
13 Broken pipe
$ SIGALRM
14 Alarm clock
$ SIGTERM
15 Terminated
$ SIGURG
16 Urgent I/O condition
$ SIGSTOP
17 Suspended (signal)
$ SIGTSTP
18 Suspended
$ SIGCONT
19 Continued
$ SIGCHLD
20 Child exited
$ SIGTTIN
21 Stopped (tty input)
$ SIGTTOU
22 Stopped (tty output)
$ SIGIO
23 I/O possible
$ SIGXCPU
24 Cputime limit exceeded
$ SIGXFSZ
25 Filesize limit exceeded
$ SIGVTALRM
26 Virtual timer expired
$ SIGPROF
27 Profiling timer expired
$ SIGWINCH
28 Window size changes
$ SIGINFO
29 Information request
$ SIGUSR1
30 User defined signal 1
$ SIGUSR2
31 User defined signal 2
$
$ gai_strerror() support catalog
$
$set 3
$ 1 (obsolete)
1 Address family for hostname not supported
$ EAI_AGAIN
2 Temporary failure in name resolution
$ EAI_BADFLAGS
3 Invalid value for ai_flags
$ EAI_FAIL
4 Non-recoverable failure in name resolution
$ EAI_FAMILY
5 ai_family not supported
$ EAI_MEMORY
6 Memory allocation failure
$ 7 (obsolete)
7 No address associated with hostname
$ EAI_NONAME
8 hostname nor servname provided, or not known
$ EAI_SERVICE
9 servname not supported for ai_socktype
$ EAI_SOCKTYPE
10 ai_socktype not supported
$ EAI_SYSTEM
11 System error returned in errno
$ EAI_BADHINTS
12 Invalid value for hints
$ EAI_PROTOCOL
13 Resolved protocol is unknown
$ EAI_OVERFLOW
14 Argument buffer overflow
$ 0
32766 Success
$ NL_MSGMAX
32767 Unknown error
