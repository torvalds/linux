.\"
.\" Must use -- tbl and pic -- with this one
.\"
.\" @(#)rpc.prog.ms	2.3 88/08/11 4.0 RPCSRC
.\" $FreeBSD$
.\"
.de BT
.if \\n%=1 .tl ''- % -''
..
.nr OF 0
.ND
.\" prevent excess underlining in nroff
.if n .fp 2 R
.OH 'Remote Procedure Call Programming Guide''Page %'
.EH 'Page %''Remote Procedure Call Programming Guide'
.SH
\&Remote Procedure Call Programming Guide
.nr OF 1
.IX "Network Programming" "" "" "" PAGE MAJOR
.IX "RPC Programming Guide"
.LP
This document assumes a working knowledge of network theory.  It is
intended for programmers who wish to write network applications using
remote procedure calls (explained below), and who want to understand
the RPC mechanisms usually hidden by the
.I rpcgen(1)
protocol compiler.
.I rpcgen
is described in detail in the previous chapter, the
.I "\fBrpcgen\fP \fIProgramming Guide\fP".
.SH
Note:
.I
.IX rpcgen "" \fIrpcgen\fP
Before attempting to write a network application, or to convert an
existing non-network application to run over the network, you may want to
understand the material in this chapter.  However, for most applications,
you can circumvent the need to cope with the details presented here by using
.I rpcgen .
The
.I "Generating XDR Routines"
section of that chapter contains the complete source for a working RPC
service\(ema remote directory listing service which uses
.I rpcgen
to generate XDR routines as well as client and server stubs.
.LP
.LP
What are remote procedure calls?  Simply put, they are the high-level
communications paradigm used in the operating system.
RPC presumes the existence of
low-level networking mechanisms (such as TCP/IP and UDP/IP), and upon them
it implements a logical client to server communications system designed
specifically for the support of network applications.  With RPC, the client
makes a procedure call to send a data packet to the server.  When the
packet arrives, the server calls a dispatch routine, performs whatever
service is requested, sends back the reply, and the procedure call returns
to the client.
.NH 0
\&Layers of RPC
.IX "layers of RPC"
.IX "RPC" "layers"
.LP
The RPC interface can be seen as being divided into three layers.\**
.FS
For a complete specification of the routines in the remote procedure
call Library, see the
.I rpc(3N)
manual page.
.FE
.LP
.I "The Highest Layer:"
.IX RPC "The Highest Layer"
The highest layer is totally transparent to the operating system,
machine and network upon which it is run.  It's probably best to
think of this level as a way of
.I using
RPC, rather than as
a \fIpart of\fP RPC proper.  Programmers who write RPC routines
should (almost) always make this layer available to others by way
of a simple C front end that entirely hides the networking.
.LP
To illustrate, at this level a program can simply make a call to
.I rnusers (),
a C routine which returns the number of users on a remote machine.
The user is not explicitly aware of using RPC \(em they simply
call a procedure, just as they would call
.I malloc() .
.LP
.I "The Middle Layer:"
.IX RPC "The Middle Layer"
The middle layer is really \*QRPC proper.\*U  Here, the user doesn't
need to consider details about sockets, the UNIX system, or other low-level
implementation mechanisms.  They simply make remote procedure calls
to routines on other machines.  The selling point here is simplicity.
It's this layer that allows RPC to pass the \*Qhello world\*U test \(em
simple things should be simple.  The middle-layer routines are used
for most applications.
.LP
RPC calls are made with the system routines
.I registerrpc()
.I callrpc()
and
.I svc_run ().
The first two of these are the most fundamental:
.I registerrpc()
obtains a unique system-wide procedure-identification number, and
.I callrpc()
actually executes a remote procedure call.  At the middle level, a
call to
.I rnusers()
is implemented by way of these two routines.
.LP
The middle layer is unfortunately rarely used in serious programming
due to its inflexibility (simplicity).  It does not allow timeout
specifications or the choice of transport.  It allows no UNIX
process control or flexibility in case of errors.  It doesn't support
multiple kinds of call authentication.  The programmer rarely needs
all these kinds of control, but one or two of them is often necessary.
.LP
.I "The Lowest Layer:"
.IX RPC "The Lowest Layer"
The lowest layer does allow these details to be controlled by the
programmer, and for that reason it is often necessary.  Programs
written at this level are also most efficient, but this is rarely a
real issue \(em since RPC clients and servers rarely generate
heavy network loads.
.LP
Although this document only discusses the interface to C,
remote procedure calls can be made from any language.
Even though this document discusses RPC
when it is used to communicate
between processes on different machines,
it works just as well for communication
between different processes on the same machine.
.br
.KS
.NH 2
\&The RPC Paradigm
.IX RPC paradigm
.LP
Here is a diagram of the RPC paradigm:
.LP
\fBFigure 1-1\fI Network Communication with the Remote Reocedure Call\fR
.LP
.PS
L1: arrow down 1i "client " rjust "program " rjust
L2: line right 1.5i "\fIcallrpc\fP" "function"
move up 1.5i; line dotted down 6i; move up 4.5i
arrow right 1i
L3: arrow down 1i "invoke " rjust "service " rjust
L4: arrow right 1.5i "call" "service"
L5: arrow down 1i " service" ljust " executes" ljust
L6: arrow left 1.5i "\fIreturn\fP" "answer"
L7: arrow down 1i "request " rjust "completed " rjust
L8: line left 1i
arrow left 1.5i "\fIreturn\fP" "reply"
L9: arrow down 1i "program " rjust "continues " rjust
line dashed down from L2 to L9
line dashed down from L4 to L7
line dashed up 1i from L3 "service " rjust "daemon " rjust
arrow dashed down 1i from L8
move right 1i from L3
box invis "Machine B"
move left 1.2i from L2; move down
box invis "Machine A"
.PE
.KE
.KS
.NH 1
\&Higher Layers of RPC
.NH 2
\&Highest Layer
.IX "highest layer of RPC"
.IX RPC "highest layer"
.LP
Imagine you're writing a program that needs to know
how many users are logged into a remote machine.
You can do this by calling the RPC library routine
.I rnusers()
as illustrated below:
.ie t .DS
.el .DS L
.ft CW
#include <stdio.h>

main(argc, argv)
	int argc;
	char **argv;
{
	int num;

	if (argc != 2) {
		fprintf(stderr, "usage: rnusers hostname\en");
		exit(1);
	}
	if ((num = rnusers(argv[1])) < 0) {
		fprintf(stderr, "error: rnusers\en");
		exit(-1);
	}
	printf("%d users on %s\en", num, argv[1]);
	exit(0);
}
.DE
.KE
RPC library routines such as
.I rnusers()
are in the RPC services library
.I librpcsvc.a
Thus, the program above should be compiled with
.DS
.ft CW
% cc \fIprogram.c -lrpcsvc\fP
.DE
.I rnusers (),
like the other RPC library routines, is documented in section 3R
of the
.I "System Interface Manual for the Sun Workstation" ,
the same section which documents the standard Sun RPC services.
.IX "RPC Services"
See the
.I intro(3R)
manual page for an explanation of the documentation strategy
for these services and their RPC protocols.
.LP
Here are some of the RPC service library routines available to the
C programmer:
.LP
\fBTable 3-3\fI RPC Service Library Routines\fR
.TS
box tab (&) ;
cfI cfI
lfL l .
Routine&Description
_
.sp .5
rnusers&Return number of users on remote machine
rusers&Return information about users on remote machine
havedisk&Determine if remote machine has disk
rstats&Get performance data from remote kernel
rwall&Write to specified remote machines
yppasswd&Update user password in Yellow Pages
.TE
.LP
Other RPC services \(em for example
.I ether()
.I mount
.I rquota()
and
.I spray
\(em are not available to the C programmer as library routines.
They do, however,
have RPC program numbers so they can be invoked with
.I callrpc()
which will be discussed in the next section.  Most of them also
have compilable
.I rpcgen(1)
protocol description files.  (The
.I rpcgen
protocol compiler radically simplifies the process of developing
network applications.
See the \fBrpcgen\fI Programming Guide\fR
for detailed information about
.I rpcgen
and
.I rpcgen
protocol description files).
.KS
.NH 2
\&Intermediate Layer
.IX "intermediate layer of RPC"
.IX "RPC" "intermediate layer"
.LP
The simplest interface, which explicitly makes RPC calls, uses the
functions
.I callrpc()
and
.I registerrpc()
Using this method, the number of remote users can be gotten as follows:
.ie t .DS
.el .DS L
#include <stdio.h>
#include <rpc/rpc.h>
#include <utmp.h>
#include <rpcsvc/rusers.h>

main(argc, argv)
	int argc;
	char **argv;
{
	unsigned long nusers;
	int stat;

	if (argc != 2) {
		fprintf(stderr, "usage: nusers hostname\en");
		exit(-1);
	}
	if (stat = callrpc(argv[1],
	  RUSERSPROG, RUSERSVERS, RUSERSPROC_NUM,
	  xdr_void, 0, xdr_u_long, &nusers) != 0) {
		clnt_perrno(stat);
		exit(1);
	}
	printf("%d users on %s\en", nusers, argv[1]);
	exit(0);
}
.DE
.KE
Each RPC procedure is uniquely defined by a program number,
version number, and procedure number.  The program number
specifies a group of related remote procedures, each of
which has a different procedure number.  Each program also
has a version number, so when a minor change is made to a
remote service (adding a new procedure, for example), a new
program number doesn't have to be assigned.  When you want
to call a procedure to find the number of remote users, you
look up the appropriate program, version and procedure numbers
in a manual, just as you look up the name of a memory allocator
when you want to allocate memory.
.LP
The simplest way of making remote procedure calls is with the RPC
library routine
.I callrpc()
It has eight parameters.  The first is the name of the remote server
machine.  The next three parameters are the program, version, and procedure
numbers\(emtogether they identify the procedure to be called.
The fifth and sixth parameters are an XDR filter and an argument to
be encoded and passed to the remote procedure.
The final two parameters are a filter for decoding the results
returned by the remote procedure and a pointer to the place where
the procedure's results are to be stored.  Multiple arguments and
results are handled by embedding them in structures.  If
.I callrpc()
completes successfully, it returns zero; else it returns a nonzero
value.  The return codes (of type
.IX "enum clnt_stat (in RPC programming)" "" "\fIenum clnt_stat\fP (in RPC programming)"
cast into an integer) are found in
.I <rpc/clnt.h> .
.LP
Since data types may be represented differently on different machines,
.I callrpc()
needs both the type of the RPC argument, as well as
a pointer to the argument itself (and similarly for the result).  For
.I RUSERSPROC_NUM ,
the return value is an
.I "unsigned long"
so
.I callrpc()
has
.I xdr_u_long()
as its first return parameter, which says
that the result is of type
.I "unsigned long"
and
.I &nusers
as its second return parameter,
which is a pointer to where the long result will be placed.  Since
.I RUSERSPROC_NUM
takes no argument, the argument parameter of
.I callrpc()
is
.I xdr_void ().
.LP
After trying several times to deliver a message, if
.I callrpc()
gets no answer, it returns with an error code.
The delivery mechanism is UDP,
which stands for User Datagram Protocol.
Methods for adjusting the number of retries
or for using a different protocol require you to use the lower
layer of the RPC library, discussed later in this document.
The remote server procedure
corresponding to the above might look like this:
.ie t .DS
.el .DS L
.ft CW
.ft CW
char *
nuser(indata)
	char *indata;
{
	unsigned long nusers;

.ft I
	/*
	 * Code here to compute the number of users
	 * and place result in variable \fInusers\fP.
	 */
.ft CW
	return((char *)&nusers);
}
.DE
.LP
It takes one argument, which is a pointer to the input
of the remote procedure call (ignored in our example),
and it returns a pointer to the result.
In the current version of C,
character pointers are the generic pointers,
so both the input argument and the return value are cast to
.I "char *" .
.LP
Normally, a server registers all of the RPC calls it plans
to handle, and then goes into an infinite loop waiting to service requests.
In this example, there is only a single procedure
to register, so the main body of the server would look like this:
.ie t .DS
.el .DS L
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>
#include <utmp.h>
#include <rpcsvc/rusers.h>

char *nuser();

main()
{
	registerrpc(RUSERSPROG, RUSERSVERS, RUSERSPROC_NUM,
		nuser, xdr_void, xdr_u_long);
	svc_run();		/* \fINever returns\fP */
	fprintf(stderr, "Error: svc_run returned!\en");
	exit(1);
}
.DE
.LP
The
.I registerrpc()
routine registers a C procedure as corresponding to a
given RPC procedure number.  The first three parameters,
.I RUSERPROG ,
.I RUSERSVERS ,
and
.I RUSERSPROC_NUM
are the program, version, and procedure numbers
of the remote procedure to be registered;
.I nuser()
is the name of the local procedure that implements the remote
procedure; and
.I xdr_void()
and
.I xdr_u_long()
are the XDR filters for the remote procedure's arguments and
results, respectively.  (Multiple arguments or multiple results
are passed as structures).
.LP
Only the UDP transport mechanism can use
.I registerrpc()
thus, it is always safe in conjunction with calls generated by
.I callrpc() .
.SH
.IX "UDP 8K warning"
Warning: the UDP transport mechanism can only deal with
arguments and results less than 8K bytes in length.
.LP
.LP
After registering the local procedure, the server program's
main procedure calls
.I svc_run (),
the RPC library's remote procedure dispatcher.  It is this
function that calls the remote procedures in response to RPC
call messages.  Note that the dispatcher takes care of decoding
remote procedure arguments and encoding results, using the XDR
filters specified when the remote procedure was registered.
.NH 2
\&Assigning Program Numbers
.IX "program number assignment"
.IX "assigning program numbers"
.LP
Program numbers are assigned in groups of
.I 0x20000000
according to the following chart:
.DS
.ft CW
       0x0 - 0x1fffffff	\fRDefined by Sun\fP
0x20000000 - 0x3fffffff	\fRDefined by user\fP
0x40000000 - 0x5fffffff	\fRTransient\fP
0x60000000 - 0x7fffffff	\fRReserved\fP
0x80000000 - 0x9fffffff	\fRReserved\fP
0xa0000000 - 0xbfffffff	\fRReserved\fP
0xc0000000 - 0xdfffffff	\fRReserved\fP
0xe0000000 - 0xffffffff	\fRReserved\fP
.ft R
.DE
Sun Microsystems administers the first group of numbers, which
should be identical for all Sun customers.  If a customer
develops an application that might be of general interest, that
application should be given an assigned number in the first
range.  The second group of numbers is reserved for specific
customer applications.  This range is intended primarily for
debugging new programs.  The third group is reserved for
applications that generate program numbers dynamically.  The
final groups are reserved for future use, and should not be
used.
.LP
To register a protocol specification, send a request by network
mail to
.I rpc@sun
or write to:
.DS
RPC Administrator
Sun Microsystems
2550 Garcia Ave.
Mountain View, CA 94043
.DE
Please include a compilable
.I rpcgen
\*Q.x\*U file describing your protocol.
You will be given a unique program number in return.
.IX RPC administration
.IX administration "of RPC"
.LP
The RPC program numbers and protocol specifications
of standard Sun RPC services can be
found in the include files in
.I "/usr/include/rpcsvc" .
These services, however, constitute only a small subset
of those which have been registered.  The complete list of
registered programs, as of the time when this manual was
printed, is:
.LP
\fBTable 3-2\fI RPC Registered Programs\fR
.TS H
box tab (&) ;
lfBI lfBI lfBI
lfL lfL lfI .
RPC Number&Program&Description
_
.TH
.sp .5
100000&PMAPPROG&portmapper
100001&RSTATPROG&remote stats
100002&RUSERSPROG&remote users
100003&NFSPROG&nfs
100004&YPPROG&Yellow Pages
100005&MOUNTPROG&mount daemon
100006&DBXPROG&remote dbx
100007&YPBINDPROG&yp binder
100008&WALLPROG&shutdown msg
100009&YPPASSWDPROG&yppasswd server
100010&ETHERSTATPROG&ether stats
100011&RQUOTAPROG&disk quotas
100012&SPRAYPROG&spray packets
100013&IBM3270PROG&3270 mapper
100014&IBMRJEPROG&RJE mapper
100015&SELNSVCPROG&selection service
100016&RDATABASEPROG&remote database access
100017&REXECPROG&remote execution
100018&ALICEPROG&Alice Office Automation
100019&SCHEDPROG&scheduling service
100020&LOCKPROG&local lock manager
100021&NETLOCKPROG&network lock manager
100022&X25PROG&x.25 inr protocol
100023&STATMON1PROG&status monitor 1
100024&STATMON2PROG&status monitor 2
100025&SELNLIBPROG&selection library
100026&BOOTPARAMPROG&boot parameters service
100027&MAZEPROG&mazewars game
100028&YPUPDATEPROG&yp update
100029&KEYSERVEPROG&key server
100030&SECURECMDPROG&secure login
100031&NETFWDIPROG&nfs net forwarder init
100032&NETFWDTPROG&nfs net forwarder trans
100033&SUNLINKMAP_PROG&sunlink MAP
100034&NETMONPROG&network monitor
100035&DBASEPROG&lightweight database
100036&PWDAUTHPROG&password authorization
100037&TFSPROG&translucent file svc
100038&NSEPROG&nse server
100039&NSE_ACTIVATE_PROG&nse activate daemon
.sp .2i
150001&PCNFSDPROG&pc passwd authorization
.sp .2i
200000&PYRAMIDLOCKINGPROG&Pyramid-locking
200001&PYRAMIDSYS5&Pyramid-sys5
200002&CADDS_IMAGE&CV cadds_image
.sp .2i
300001&ADT_RFLOCKPROG&ADT file locking
.TE
.NH 2
\&Passing Arbitrary Data Types
.IX "arbitrary data types"
.LP
In the previous example, the RPC call passes a single
.I "unsigned long"
RPC can handle arbitrary data structures, regardless of
different machines' byte orders or structure layout conventions,
by always converting them to a network standard called
.I "External Data Representation"
(XDR) before
sending them over the wire.
The process of converting from a particular machine representation
to XDR format is called
.I serializing ,
and the reverse process is called
.I deserializing .
The type field parameters of
.I callrpc()
and
.I registerrpc()
can be a built-in procedure like
.I xdr_u_long()
in the previous example, or a user supplied one.
XDR has these built-in type routines:
.IX RPC "built-in routines"
.DS
.ft CW
xdr_int()      xdr_u_int()      xdr_enum()
xdr_long()     xdr_u_long()     xdr_bool()
xdr_short()    xdr_u_short()    xdr_wrapstring()
xdr_char()     xdr_u_char()
.DE
Note that the routine
.I xdr_string()
exists, but cannot be used with
.I callrpc()
and
.I registerrpc (),
which only pass two parameters to their XDR routines.
.I xdr_wrapstring()
has only two parameters, and is thus OK.  It calls
.I xdr_string ().
.LP
As an example of a user-defined type routine,
if you wanted to send the structure
.DS
.ft CW
struct simple {
	int a;
	short b;
} simple;
.DE
then you would call
.I callrpc()
as
.DS
.ft CW
callrpc(hostname, PROGNUM, VERSNUM, PROCNUM,
        xdr_simple, &simple ...);
.DE
where
.I xdr_simple()
is written as:
.ie t .DS
.el .DS L
.ft CW
#include <rpc/rpc.h>

xdr_simple(xdrsp, simplep)
	XDR *xdrsp;
	struct simple *simplep;
{
	if (!xdr_int(xdrsp, &simplep->a))
		return (0);
	if (!xdr_short(xdrsp, &simplep->b))
		return (0);
	return (1);
}
.DE
.LP
An XDR routine returns nonzero (true in the sense of C) if it
completes successfully, and zero otherwise.
A complete description of XDR is in the
.I "XDR Protocol Specification"
section of this manual, only few implementation examples are
given here.
.LP
In addition to the built-in primitives,
there are also the prefabricated building blocks:
.DS
.ft CW
xdr_array()       xdr_bytes()     xdr_reference()
xdr_vector()      xdr_union()     xdr_pointer()
xdr_string()      xdr_opaque()
.DE
To send a variable array of integers,
you might package them up as a structure like this
.DS
.ft CW
struct varintarr {
	int *data;
	int arrlnth;
} arr;
.DE
and make an RPC call such as
.DS
.ft CW
callrpc(hostname, PROGNUM, VERSNUM, PROCNUM,
        xdr_varintarr, &arr...);
.DE
with
.I xdr_varintarr()
defined as:
.ie t .DS
.el .DS L
.ft CW
xdr_varintarr(xdrsp, arrp)
	XDR *xdrsp;
	struct varintarr *arrp;
{
	return (xdr_array(xdrsp, &arrp->data, &arrp->arrlnth,
		MAXLEN, sizeof(int), xdr_int));
}
.DE
This routine takes as parameters the XDR handle,
a pointer to the array, a pointer to the size of the array,
the maximum allowable array size,
the size of each array element,
and an XDR routine for handling each array element.
.KS
.LP
If the size of the array is known in advance, one can use
.I xdr_vector (),
which serializes fixed-length arrays.
.ie t .DS
.el .DS L
.ft CW
int intarr[SIZE];

xdr_intarr(xdrsp, intarr)
	XDR *xdrsp;
	int intarr[];
{
	int i;

	return (xdr_vector(xdrsp, intarr, SIZE, sizeof(int),
		xdr_int));
}
.DE
.KE
.LP
XDR always converts quantities to 4-byte multiples when serializing.
Thus, if either of the examples above involved characters
instead of integers, each character would occupy 32 bits.
That is the reason for the XDR routine
.I xdr_bytes()
which is like
.I xdr_array()
except that it packs characters;
.I xdr_bytes()
has four parameters, similar to the first four parameters of
.I xdr_array ().
For null-terminated strings, there is also the
.I xdr_string()
routine, which is the same as
.I xdr_bytes()
without the length parameter.
On serializing it gets the string length from
.I strlen (),
and on deserializing it creates a null-terminated string.
.LP
Here is a final example that calls the previously written
.I xdr_simple()
as well as the built-in functions
.I xdr_string()
and
.I xdr_reference (),
which chases pointers:
.ie t .DS
.el .DS L
.ft CW
struct finalexample {
	char *string;
	struct simple *simplep;
} finalexample;

xdr_finalexample(xdrsp, finalp)
	XDR *xdrsp;
	struct finalexample *finalp;
{

	if (!xdr_string(xdrsp, &finalp->string, MAXSTRLEN))
		return (0);
	if (!xdr_reference(xdrsp, &finalp->simplep,
	  sizeof(struct simple), xdr_simple);
		return (0);
	return (1);
}
.DE
Note that we could as easily call
.I xdr_simple()
here instead of
.I xdr_reference ().
.NH 1
\&Lowest Layer of RPC
.IX "lowest layer of RPC"
.IX "RPC" "lowest layer"
.LP
In the examples given so far,
RPC takes care of many details automatically for you.
In this section, we'll show you how you can change the defaults
by using lower layers of the RPC library.
It is assumed that you are familiar with sockets
and the system calls for dealing with them.
.LP
There are several occasions when you may need to use lower layers of
RPC.  First, you may need to use TCP, since the higher layer uses UDP,
which restricts RPC calls to 8K bytes of data.  Using TCP permits calls
to send long streams of data.
For an example, see the
.I TCP
section below.  Second, you may want to allocate and free memory
while serializing or deserializing with XDR routines.
There is no call at the higher level to let
you free memory explicitly.
For more explanation, see the
.I "Memory Allocation with XDR"
section below.
Third, you may need to perform authentication
on either the client or server side, by supplying
credentials or verifying them.
See the explanation in the
.I Authentication
section below.
.NH 2
\&More on the Server Side
.IX RPC "server side"
.LP
The server for the
.I nusers()
program shown below does the same thing as the one using
.I registerrpc()
above, but is written using a lower layer of the RPC package:
.ie t .DS
.el .DS L
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>
#include <utmp.h>
#include <rpcsvc/rusers.h>

main()
{
	SVCXPRT *transp;
	int nuser();

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL){
		fprintf(stderr, "can't create an RPC server\en");
		exit(1);
	}
	pmap_unset(RUSERSPROG, RUSERSVERS);
	if (!svc_register(transp, RUSERSPROG, RUSERSVERS,
			  nuser, IPPROTO_UDP)) {
		fprintf(stderr, "can't register RUSER service\en");
		exit(1);
	}
	svc_run();  /* \fINever returns\fP */
	fprintf(stderr, "should never reach this point\en");
}

nuser(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	unsigned long nusers;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (!svc_sendreply(transp, xdr_void, 0))
			fprintf(stderr, "can't reply to RPC call\en");
		return;
	case RUSERSPROC_NUM:
.ft I
		/*
		 * Code here to compute the number of users
		 * and assign it to the variable \fInusers\fP
		 */
.ft CW
		if (!svc_sendreply(transp, xdr_u_long, &nusers))
			fprintf(stderr, "can't reply to RPC call\en");
		return;
	default:
		svcerr_noproc(transp);
		return;
	}
}
.DE
.LP
First, the server gets a transport handle, which is used
for receiving and replying to RPC messages.
.I registerrpc()
uses
.I svcudp_create()
to get a UDP handle.
If you require a more reliable protocol, call
.I svctcp_create()
instead.
If the argument to
.I svcudp_create()
is
.I RPC_ANYSOCK
the RPC library creates a socket
on which to receive and reply to RPC calls.  Otherwise,
.I svcudp_create()
expects its argument to be a valid socket number.
If you specify your own socket, it can be bound or unbound.
If it is bound to a port by the user, the port numbers of
.I svcudp_create()
and
.I clnttcp_create()
(the low-level client routine) must match.
.LP
If the user specifies the
.I RPC_ANYSOCK
argument, the RPC library routines will open sockets.
Otherwise they will expect the user to do so.  The routines
.I svcudp_create()
and
.I clntudp_create()
will cause the RPC library routines to
.I bind()
their socket if it is not bound already.
.LP
A service may choose to register its port number with the
local portmapper service.  This is done is done by specifying
a non-zero protocol number in
.I svc_register ().
Incidently, a client can discover the server's port number by
consulting the portmapper on their server's machine.  This can
be done automatically by specifying a zero port number in
.I clntudp_create()
or
.I clnttcp_create ().
.LP
After creating an
.I SVCXPRT ,
the next step is to call
.I pmap_unset()
so that if the
.I nusers()
server crashed earlier,
any previous trace of it is erased before restarting.
More precisely,
.I pmap_unset()
erases the entry for
.I RUSERSPROG
from the port mapper's tables.
.LP
Finally, we associate the program number for
.I nusers()
with the procedure
.I nuser ().
The final argument to
.I svc_register()
is normally the protocol being used,
which, in this case, is
.I IPPROTO_UDP
Notice that unlike
.I registerrpc (),
there are no XDR routines involved
in the registration process.
Also, registration is done on the program,
rather than procedure, level.
.LP
The user routine
.I nuser()
must call and dispatch the appropriate XDR routines
based on the procedure number.
Note that
two things are handled by
.I nuser()
that
.I registerrpc()
handles automatically.
The first is that procedure
.I NULLPROC
(currently zero) returns with no results.
This can be used as a simple test
for detecting if a remote program is running.
Second, there is a check for invalid procedure numbers.
If one is detected,
.I svcerr_noproc()
is called to handle the error.
.KS
.LP
The user service routine serializes the results and returns
them to the RPC caller via
.I svc_sendreply()
Its first parameter is the
.I SVCXPRT
handle, the second is the XDR routine,
and the third is a pointer to the data to be returned.
Not illustrated above is how a server
handles an RPC program that receives data.
As an example, we can add a procedure
.I RUSERSPROC_BOOL
which has an argument
.I nusers (),
and returns
.I TRUE
or
.I FALSE
depending on whether there are nusers logged on.
It would look like this:
.ie t .DS
.el .DS L
.ft CW
case RUSERSPROC_BOOL: {
	int bool;
	unsigned nuserquery;

	if (!svc_getargs(transp, xdr_u_int, &nuserquery) {
		svcerr_decode(transp);
		return;
	}
.ft I
	/*
	 * Code to set \fInusers\fP = number of users
	 */
.ft CW
	if (nuserquery == nusers)
		bool = TRUE;
	else
		bool = FALSE;
	if (!svc_sendreply(transp, xdr_bool, &bool)) {
		 fprintf(stderr, "can't reply to RPC call\en");
		 return (1);
	}
	return;
}
.DE
.KE
.LP
The relevant routine is
.I svc_getargs()
which takes an
.I SVCXPRT
handle, the XDR routine,
and a pointer to where the input is to be placed as arguments.
.NH 2
\&Memory Allocation with XDR
.IX "memory allocation with XDR"
.IX XDR "memory allocation"
.LP
XDR routines not only do input and output,
they also do memory allocation.
This is why the second parameter of
.I xdr_array()
is a pointer to an array, rather than the array itself.
If it is
.I NULL ,
then
.I xdr_array()
allocates space for the array and returns a pointer to it,
putting the size of the array in the third argument.
As an example, consider the following XDR routine
.I xdr_chararr1()
which deals with a fixed array of bytes with length
.I SIZE .
.ie t .DS
.el .DS L
.ft CW
xdr_chararr1(xdrsp, chararr)
	XDR *xdrsp;
	char chararr[];
{
	char *p;
	int len;

	p = chararr;
	len = SIZE;
	return (xdr_bytes(xdrsp, &p, &len, SIZE));
}
.DE
If space has already been allocated in
.I chararr ,
it can be called from a server like this:
.ie t .DS
.el .DS L
.ft CW
char chararr[SIZE];

svc_getargs(transp, xdr_chararr1, chararr);
.DE
If you want XDR to do the allocation,
you would have to rewrite this routine in the following way:
.ie t .DS
.el .DS L
.ft CW
xdr_chararr2(xdrsp, chararrp)
	XDR *xdrsp;
	char **chararrp;
{
	int len;

	len = SIZE;
	return (xdr_bytes(xdrsp, charrarrp, &len, SIZE));
}
.DE
Then the RPC call might look like this:
.ie t .DS
.el .DS L
.ft CW
char *arrptr;

arrptr = NULL;
svc_getargs(transp, xdr_chararr2, &arrptr);
.ft I
/*
 * Use the result here
 */
.ft CW
svc_freeargs(transp, xdr_chararr2, &arrptr);
.DE
Note that, after being used, the character array can be freed with
.I svc_freeargs()
.I svc_freeargs()
will not attempt to free any memory if the variable indicating it
is NULL.  For example, in the routine
.I xdr_finalexample (),
given earlier, if
.I finalp->string
was NULL, then it would not be freed.  The same is true for
.I finalp->simplep .
.LP
To summarize, each XDR routine is responsible
for serializing, deserializing, and freeing memory.
When an XDR routine is called from
.I callrpc()
the serializing part is used.
When called from
.I svc_getargs()
the deserializer is used.
And when called from
.I svc_freeargs()
the memory deallocator is used.  When building simple examples like those
in this section, a user doesn't have to worry
about the three modes.
See the
.I "External Data Representation: Sun Technical Notes"
for examples of more sophisticated XDR routines that determine
which of the three modes they are in and adjust their behavior accordingly.
.KS
.NH 2
\&The Calling Side
.IX RPC "calling side"
.LP
When you use
.I callrpc()
you have no control over the RPC delivery
mechanism or the socket used to transport the data.
To illustrate the layer of RPC that lets you adjust these
parameters, consider the following code to call the
.I nusers
service:
.ie t .DS
.el .DS L
.ft CW
.vs 11
#include <stdio.h>
#include <rpc/rpc.h>
#include <utmp.h>
#include <rpcsvc/rusers.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

main(argc, argv)
	int argc;
	char **argv;
{
	struct hostent *hp;
	struct timeval pertry_timeout, total_timeout;
	struct sockaddr_in server_addr;
	int sock = RPC_ANYSOCK;
	register CLIENT *client;
	enum clnt_stat clnt_stat;
	unsigned long nusers;

	if (argc != 2) {
		fprintf(stderr, "usage: nusers hostname\en");
		exit(-1);
	}
	if ((hp = gethostbyname(argv[1])) == NULL) {
		fprintf(stderr, "can't get addr for %s\en",argv[1]);
		exit(-1);
	}
	pertry_timeout.tv_sec = 3;
	pertry_timeout.tv_usec = 0;
	bcopy(hp->h_addr, (caddr_t)&server_addr.sin_addr,
		hp->h_length);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port =  0;
	if ((client = clntudp_create(&server_addr, RUSERSPROG,
	  RUSERSVERS, pertry_timeout, &sock)) == NULL) {
		clnt_pcreateerror("clntudp_create");
		exit(-1);
	}
	total_timeout.tv_sec = 20;
	total_timeout.tv_usec = 0;
	clnt_stat = clnt_call(client, RUSERSPROC_NUM, xdr_void,
		0, xdr_u_long, &nusers, total_timeout);
	if (clnt_stat != RPC_SUCCESS) {
		clnt_perror(client, "rpc");
		exit(-1);
	}
	clnt_destroy(client);
	close(sock);
	exit(0);
}
.vs
.DE
.KE
The low-level version of
.I callrpc()
is
.I clnt_call()
which takes a
.I CLIENT
pointer rather than a host name.  The parameters to
.I clnt_call()
are a
.I CLIENT
pointer, the procedure number,
the XDR routine for serializing the argument,
a pointer to the argument,
the XDR routine for deserializing the return value,
a pointer to where the return value will be placed,
and the time in seconds to wait for a reply.
.LP
The
.I CLIENT
pointer is encoded with the transport mechanism.
.I callrpc()
uses UDP, thus it calls
.I clntudp_create()
to get a
.I CLIENT
pointer.  To get TCP (Transmission Control Protocol), you would use
.I clnttcp_create() .
.LP
The parameters to
.I clntudp_create()
are the server address, the program number, the version number,
a timeout value (between tries), and a pointer to a socket.
The final argument to
.I clnt_call()
is the total time to wait for a response.
Thus, the number of tries is the
.I clnt_call()
timeout divided by the
.I clntudp_create()
timeout.
.LP
Note that the
.I clnt_destroy()
call
always deallocates the space associated with the
.I CLIENT
handle.  It closes the socket associated with the
.I CLIENT
handle, however, only if the RPC library opened it.  It the
socket was opened by the user, it stays open.  This makes it
possible, in cases where there are multiple client handles
using the same socket, to destroy one handle without closing
the socket that other handles are using.
.LP
To make a stream connection, the call to
.I clntudp_create()
is replaced with a call to
.I clnttcp_create() .
.DS
.ft CW
clnttcp_create(&server_addr, prognum, versnum, &sock,
               inputsize, outputsize);
.DE
There is no timeout argument; instead, the receive and send buffer
sizes must be specified.  When the
.I clnttcp_create()
call is made, a TCP connection is established.
All RPC calls using that
.I CLIENT
handle would use this connection.
The server side of an RPC call using TCP has
.I svcudp_create()
replaced by
.I svctcp_create() .
.DS
.ft CW
transp = svctcp_create(RPC_ANYSOCK, 0, 0);
.DE
The last two arguments to
.I svctcp_create()
are send and receive sizes respectively.  If `0' is specified for
either of these, the system chooses a reasonable default.
.KS
.NH 1
\&Other RPC Features
.IX "RPC" "miscellaneous features"
.IX "miscellaneous RPC features"
.LP
This section discusses some other aspects of RPC
that are occasionally useful.
.NH 2
\&Select on the Server Side
.IX RPC select() RPC \fIselect()\fP
.IX select() "" \fIselect()\fP "on the server side"
.LP
Suppose a process is processing RPC requests
while performing some other activity.
If the other activity involves periodically updating a data structure,
the process can set an alarm signal before calling
.I svc_run()
But if the other activity
involves waiting on a file descriptor, the
.I svc_run()
call won't work.
The code for
.I svc_run()
is as follows:
.ie t .DS
.el .DS L
.ft CW
.vs 11
void
svc_run()
{
	fd_set readfds;
	int dtbsz = getdtablesize();

	for (;;) {
		readfds = svc_fds;
		switch (select(dtbsz, &readfds, NULL,NULL,NULL)) {

		case -1:
			if (errno == EINTR)
				continue;
			perror("select");
			return;
		case 0:
			break;
		default:
			svc_getreqset(&readfds);
		}
	}
}
.vs
.DE
.KE
.LP
You can bypass
.I svc_run()
and call
.I svc_getreqset()
yourself.
All you need to know are the file descriptors
of the socket(s) associated with the programs you are waiting on.
Thus you can have your own
.I select()
.IX select() "" \fIselect()\fP
that waits on both the RPC socket,
and your own descriptors.  Note that
.I svc_fds()
is a bit mask of all the file descriptors that RPC is using for
services.  It can change everytime that
.I any
RPC library routine is called, because descriptors are constantly
being opened and closed, for example for TCP connections.
.NH 2
\&Broadcast RPC
.IX "broadcast RPC"
.IX RPC "broadcast"
.LP
The
.I portmapper
is a daemon that converts RPC program numbers
into DARPA protocol port numbers; see the
.I portmap
man page.  You can't do broadcast RPC without the portmapper.
Here are the main differences between
broadcast RPC and normal RPC calls:
.IP  1.
Normal RPC expects one answer, whereas
broadcast RPC expects many answers
(one or more answer from each responding machine).
.IP  2.
Broadcast RPC can only be supported by packet-oriented (connectionless)
transport protocols like UPD/IP.
.IP  3.
The implementation of broadcast RPC
treats all unsuccessful responses as garbage by filtering them out.
Thus, if there is a version mismatch between the
broadcaster and a remote service,
the user of broadcast RPC never knows.
.IP  4.
All broadcast messages are sent to the portmap port.
Thus, only services that register themselves with their portmapper
are accessible via the broadcast RPC mechanism.
.IP  5.
Broadcast requests are limited in size to the MTU (Maximum Transfer
Unit) of the local network.  For Ethernet, the MTU is 1500 bytes.
.KS
.NH 3
\&Broadcast RPC Synopsis
.IX "broadcast RPC" synopsis
.IX "RPC" "broadcast synopsis"
.ie t .DS
.el .DS L
.ft CW
#include <rpc/pmap_clnt.h>
	. . .
enum clnt_stat	clnt_stat;
	. . .
clnt_stat = clnt_broadcast(prognum, versnum, procnum,
  inproc, in, outproc, out, eachresult)
	u_long    prognum;        /* \fIprogram number\fP */
	u_long    versnum;        /* \fIversion number\fP */
	u_long    procnum;        /* \fIprocedure number\fP */
	xdrproc_t inproc;         /* \fIxdr routine for args\fP */
	caddr_t   in;             /* \fIpointer to args\fP */
	xdrproc_t outproc;        /* \fIxdr routine for results\fP */
	caddr_t   out;            /* \fIpointer to results\fP */
	bool_t    (*eachresult)();/* \fIcall with each result gotten\fP */
.DE
.KE
The procedure
.I eachresult()
is called each time a valid result is obtained.
It returns a boolean that indicates
whether or not the user wants more responses.
.ie t .DS
.el .DS L
.ft CW
bool_t done;
	. . .
done = eachresult(resultsp, raddr)
	caddr_t resultsp;
	struct sockaddr_in *raddr; /* \fIAddr of responding machine\fP */
.DE
If
.I done
is
.I TRUE ,
then broadcasting stops and
.I clnt_broadcast()
returns successfully.
Otherwise, the routine waits for another response.
The request is rebroadcast
after a few seconds of waiting.
If no responses come back,
the routine returns with
.I RPC_TIMEDOUT .
.NH 2
\&Batching
.IX "batching"
.IX RPC "batching"
.LP
The RPC architecture is designed so that clients send a call message,
and wait for servers to reply that the call succeeded.
This implies that clients do not compute
while servers are processing a call.
This is inefficient if the client does not want or need
an acknowledgement for every message sent.
It is possible for clients to continue computing
while waiting for a response,
using RPC batch facilities.
.LP
RPC messages can be placed in a \*Qpipeline\*U of calls
to a desired server; this is called batching.
Batching assumes that:
1) each RPC call in the pipeline requires no response from the server,
and the server does not send a response message; and
2) the pipeline of calls is transported on a reliable
byte stream transport such as TCP/IP.
Since the server does not respond to every call,
the client can generate new calls in parallel
with the server executing previous calls.
Furthermore, the TCP/IP implementation can buffer up
many call messages, and send them to the server in one
.I write()
system call.  This overlapped execution
greatly decreases the interprocess communication overhead of
the client and server processes,
and the total elapsed time of a series of calls.
.LP
Since the batched calls are buffered,
the client should eventually do a nonbatched call
in order to flush the pipeline.
.LP
A contrived example of batching follows.
Assume a string rendering service (like a window system)
has two similar calls: one renders a string and returns void results,
while the other renders a string and remains silent.
The service (using the TCP/IP transport) may look like:
.ie t .DS
.el .DS L
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>
#include <suntool/windows.h>

void windowdispatch();

main()
{
	SVCXPRT *transp;

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL){
		fprintf(stderr, "can't create an RPC server\en");
		exit(1);
	}
	pmap_unset(WINDOWPROG, WINDOWVERS);
	if (!svc_register(transp, WINDOWPROG, WINDOWVERS,
	  windowdispatch, IPPROTO_TCP)) {
		fprintf(stderr, "can't register WINDOW service\en");
		exit(1);
	}
	svc_run();  /* \fINever returns\fP */
	fprintf(stderr, "should never reach this point\en");
}

void
windowdispatch(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	char *s = NULL;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (!svc_sendreply(transp, xdr_void, 0))
			fprintf(stderr, "can't reply to RPC call\en");
		return;
	case RENDERSTRING:
		if (!svc_getargs(transp, xdr_wrapstring, &s)) {
			fprintf(stderr, "can't decode arguments\en");
.ft I
			/*
			 * Tell caller he screwed up
			 */
.ft CW
			svcerr_decode(transp);
			break;
		}
.ft I
		/*
		 * Code here to render the string \fIs\fP
		 */
.ft CW
		if (!svc_sendreply(transp, xdr_void, NULL))
			fprintf(stderr, "can't reply to RPC call\en");
		break;
	case RENDERSTRING_BATCHED:
		if (!svc_getargs(transp, xdr_wrapstring, &s)) {
			fprintf(stderr, "can't decode arguments\en");
.ft I
			/*
			 * We are silent in the face of protocol errors
			 */
.ft CW
			break;
		}
.ft I
		/*
		 * Code here to render string s, but send no reply!
		 */
.ft CW
		break;
	default:
		svcerr_noproc(transp);
		return;
	}
.ft I
	/*
	 * Now free string allocated while decoding arguments
	 */
.ft CW
	svc_freeargs(transp, xdr_wrapstring, &s);
}
.DE
Of course the service could have one procedure
that takes the string and a boolean
to indicate whether or not the procedure should respond.
.LP
In order for a client to take advantage of batching,
the client must perform RPC calls on a TCP-based transport
and the actual calls must have the following attributes:
1) the result's XDR routine must be zero
.I NULL ),
and 2) the RPC call's timeout must be zero.
.KS
.LP
Here is an example of a client that uses batching to render a
bunch of strings; the batching is flushed when the client gets
a null string (EOF):
.ie t .DS
.el .DS L
.ft CW
.vs 11
#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <suntool/windows.h>

main(argc, argv)
	int argc;
	char **argv;
{
	struct hostent *hp;
	struct timeval pertry_timeout, total_timeout;
	struct sockaddr_in server_addr;
	int sock = RPC_ANYSOCK;
	register CLIENT *client;
	enum clnt_stat clnt_stat;
	char buf[1000], *s = buf;

	if ((client = clnttcp_create(&server_addr,
	  WINDOWPROG, WINDOWVERS, &sock, 0, 0)) == NULL) {
		perror("clnttcp_create");
		exit(-1);
	}
	total_timeout.tv_sec = 0;
	total_timeout.tv_usec = 0;
	while (scanf("%s", s) != EOF) {
		clnt_stat = clnt_call(client, RENDERSTRING_BATCHED,
			xdr_wrapstring, &s, NULL, NULL, total_timeout);
		if (clnt_stat != RPC_SUCCESS) {
			clnt_perror(client, "batched rpc");
			exit(-1);
		}
	}

	/* \fINow flush the pipeline\fP */

	total_timeout.tv_sec = 20;
	clnt_stat = clnt_call(client, NULLPROC, xdr_void, NULL,
		xdr_void, NULL, total_timeout);
	if (clnt_stat != RPC_SUCCESS) {
		clnt_perror(client, "rpc");
		exit(-1);
	}
	clnt_destroy(client);
	exit(0);
}
.vs
.DE
.KE
Since the server sends no message,
the clients cannot be notified of any of the failures that may occur.
Therefore, clients are on their own when it comes to handling errors.
.LP
The above example was completed to render
all of the (2000) lines in the file
.I /etc/termcap .
The rendering service did nothing but throw the lines away.
The example was run in the following four configurations:
1) machine to itself, regular RPC;
2) machine to itself, batched RPC;
3) machine to another, regular RPC; and
4) machine to another, batched RPC.
The results are as follows:
1) 50 seconds;
2) 16 seconds;
3) 52 seconds;
4) 10 seconds.
Running
.I fscanf()
on
.I /etc/termcap
only requires six seconds.
These timings show the advantage of protocols
that allow for overlapped execution,
though these protocols are often hard to design.
.NH 2
\&Authentication
.IX "authentication"
.IX "RPC" "authentication"
.LP
In the examples presented so far,
the caller never identified itself to the server,
and the server never required an ID from the caller.
Clearly, some network services, such as a network filesystem,
require stronger security than what has been presented so far.
.LP
In reality, every RPC call is authenticated by
the RPC package on the server, and similarly,
the RPC client package generates and sends authentication parameters.
Just as different transports (TCP/IP or UDP/IP)
can be used when creating RPC clients and servers,
different forms of authentication can be associated with RPC clients;
the default authentication type used as a default is type
.I none .
.LP
The authentication subsystem of the RPC package is open ended.
That is, numerous types of authentication are easy to support.
.NH 3
\&UNIX Authentication
.IX "UNIX Authentication"
.IP "\fIThe Client Side\fP"
.LP
When a caller creates a new RPC client handle as in:
.DS
.ft CW
clnt = clntudp_create(address, prognum, versnum,
		      wait, sockp)
.DE
the appropriate transport instance defaults
the associate authentication handle to be
.DS
.ft CW
clnt->cl_auth = authnone_create();
.DE
The RPC client can choose to use
.I UNIX
style authentication by setting
.I clnt\->cl_auth
after creating the RPC client handle:
.DS
.ft CW
clnt->cl_auth = authunix_create_default();
.DE
This causes each RPC call associated with
.I clnt
to carry with it the following authentication credentials structure:
.ie t .DS
.el .DS L
.ft I
/*
 * UNIX style credentials.
 */
.ft CW
struct authunix_parms {
    u_long  aup_time;       /* \fIcredentials creation time\fP */
    char    *aup_machname;  /* \fIhost name where client is\fP */
    int     aup_uid;        /* \fIclient's UNIX effective uid\fP */
    int     aup_gid;        /* \fIclient's current group id\fP */
    u_int   aup_len;        /* \fIelement length of aup_gids\fP */
    int     *aup_gids;      /* \fIarray of groups user is in\fP */
};
.DE
These fields are set by
.I authunix_create_default()
by invoking the appropriate system calls.
Since the RPC user created this new style of authentication,
the user is responsible for destroying it with:
.DS
.ft CW
auth_destroy(clnt->cl_auth);
.DE
This should be done in all cases, to conserve memory.
.sp
.IP "\fIThe Server Side\fP"
.LP
Service implementors have a harder time dealing with authentication issues
since the RPC package passes the service dispatch routine a request
that has an arbitrary authentication style associated with it.
Consider the fields of a request handle passed to a service dispatch routine:
.ie t .DS
.el .DS L
.ft I
/*
 * An RPC Service request
 */
.ft CW
struct svc_req {
    u_long    rq_prog;    	/* \fIservice program number\fP */
    u_long    rq_vers;    	/* \fIservice protocol vers num\fP */
    u_long    rq_proc;    	/* \fIdesired procedure number\fP */
    struct opaque_auth rq_cred; /* \fIraw credentials from wire\fP */
    caddr_t   rq_clntcred;  /* \fIcredentials (read only)\fP */
};
.DE
The
.I rq_cred
is mostly opaque, except for one field of interest:
the style or flavor of authentication credentials:
.ie t .DS
.el .DS L
.ft I
/*
 * Authentication info.  Mostly opaque to the programmer.
 */
.ft CW
struct opaque_auth {
    enum_t  oa_flavor;  /* \fIstyle of credentials\fP */
    caddr_t oa_base;    /* \fIaddress of more auth stuff\fP */
    u_int   oa_length;  /* \fInot to exceed \fIMAX_AUTH_BYTES */
};
.DE
.IX RPC guarantees
The RPC package guarantees the following
to the service dispatch routine:
.IP  1.
That the request's
.I rq_cred
is well formed.  Thus the service implementor may inspect the request's
.I rq_cred.oa_flavor
to determine which style of authentication the caller used.
The service implementor may also wish to inspect the other fields of
.I rq_cred
if the style is not one of the styles supported by the RPC package.
.IP  2.
That the request's
.I rq_clntcred
field is either
.I NULL
or points to a well formed structure
that corresponds to a supported style of authentication credentials.
Remember that only
.I unix
style is currently supported, so (currently)
.I rq_clntcred
could be cast to a pointer to an
.I authunix_parms
structure.  If
.I rq_clntcred
is
.I NULL ,
the service implementor may wish to inspect the other (opaque) fields of
.I rq_cred
in case the service knows about a new type of authentication
that the RPC package does not know about.
.LP
Our remote users service example can be extended so that
it computes results for all users except UID 16:
.ie t .DS
.el .DS L
.ft CW
.vs 11
nuser(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	struct authunix_parms *unix_cred;
	int uid;
	unsigned long nusers;

.ft I
	/*
	 * we don't care about authentication for null proc
	 */
.ft CW
	if (rqstp->rq_proc == NULLPROC) {
		if (!svc_sendreply(transp, xdr_void, 0)) {
			fprintf(stderr, "can't reply to RPC call\en");
			return (1);
		 }
		 return;
	}
.ft I
	/*
	 * now get the uid
	 */
.ft CW
	switch (rqstp->rq_cred.oa_flavor) {
	case AUTH_UNIX:
		unix_cred =
			(struct authunix_parms *)rqstp->rq_clntcred;
		uid = unix_cred->aup_uid;
		break;
	case AUTH_NULL:
	default:
		svcerr_weakauth(transp);
		return;
	}
	switch (rqstp->rq_proc) {
	case RUSERSPROC_NUM:
.ft I
		/*
		 * make sure caller is allowed to call this proc
		 */
.ft CW
		if (uid == 16) {
			svcerr_systemerr(transp);
			return;
		}
.ft I
		/*
		 * Code here to compute the number of users
		 * and assign it to the variable \fInusers\fP
		 */
.ft CW
		if (!svc_sendreply(transp, xdr_u_long, &nusers)) {
			fprintf(stderr, "can't reply to RPC call\en");
			return (1);
		}
		return;
	default:
		svcerr_noproc(transp);
		return;
	}
}
.vs
.DE
A few things should be noted here.
First, it is customary not to check
the authentication parameters associated with the
.I NULLPROC
(procedure number zero).
Second, if the authentication parameter's type is not suitable
for your service, you should call
.I svcerr_weakauth() .
And finally, the service protocol itself should return status
for access denied; in the case of our example, the protocol
does not have such a status, so we call the service primitive
.I svcerr_systemerr()
instead.
.LP
The last point underscores the relation between
the RPC authentication package and the services;
RPC deals only with
.I authentication
and not with individual services'
.I "access control" .
The services themselves must implement their own access control policies
and reflect these policies as return statuses in their protocols.
.NH 2
\&DES Authentication
.IX RPC DES
.IX RPC authentication
.LP
UNIX authentication is quite easy to defeat.  Instead of using
.I authunix_create_default (),
one can call
.I authunix_create()
and then modify the RPC authentication handle it returns by filling in
whatever user ID and hostname they wish the server to think they have.
DES authentication is thus recommended for people who want more security
than UNIX authentication offers.
.LP
The details of the DES authentication protocol are complicated and
are not explained here.
See
.I "Remote Procedure Calls: Protocol Specification"
for the details.
.LP
In  order for  DES authentication   to  work, the
.I keyserv(8c)
daemon must be running  on both  the  server  and client machines.  The
users on  these machines  need  public  keys  assigned by  the network
administrator in  the
.I publickey(5)
database.  And,  they  need to have decrypted  their  secret keys
using  their  login   password.  This automatically happens when one
logs in using
.I login(1) ,
or can be done manually using
.I keylogin(1) .
The
.I "Network Services"
chapter
.\" XXX
explains more how to setup secure networking.
.sp
.IP "\fIClient Side\fP"
.LP
If a client wishes to use DES authentication, it must set its
authentication handle appropriately.  Here is an example:
.DS
cl->cl_auth =
	authdes_create(servername, 60, &server_addr, NULL);
.DE
The first argument is the network name or \*Qnetname\*U of the owner of
the server process.  Typically, server processes are root processes
and their netname can be derived using the following call:
.DS
char servername[MAXNETNAMELEN];

host2netname(servername, rhostname, NULL);
.DE
Here,
.I rhostname
is the hostname of the machine the server process is running on.
.I host2netname()
fills in
.I servername
to contain this root process's netname.  If the
server process was run by a regular user, one could use the call
.I user2netname()
instead.  Here is an example for a server process with the same user
ID as the client:
.DS
char servername[MAXNETNAMELEN];

user2netname(servername, getuid(), NULL);
.DE
The last argument to both of these calls,
.I user2netname()
and
.I host2netname (),
is the name of the naming domain where the server is located.  The
.I NULL
used here means \*Quse the local domain name.\*U
.LP
The second argument to
.I authdes_create()
is a lifetime for the credential.  Here it is set to sixty
seconds.  What that means is that the credential will expire 60
seconds from now.  If some mischievous user tries to reuse the
credential, the server RPC subsystem will recognize that it has
expired and not grant any requests.  If the same mischievous user
tries to reuse the credential within the sixty second lifetime,
he will still be rejected because the server RPC subsystem
remembers which credentials it has already seen in the near past,
and will not grant requests to duplicates.
.LP
The third argument to
.I authdes_create()
is the address of the host to synchronize with.  In order for DES
authentication to work, the server and client must agree upon the
time.  Here we pass the address of the server itself, so the
client and server will both be using the same time: the server's
time.  The argument can be
.I NULL ,
which means \*Qdon't bother synchronizing.\*U You should only do this
if you are sure the client and server are already synchronized.
.LP
The final argument to
.I authdes_create()
is the address of a DES encryption key to use for encrypting
timestamps and data.  If this argument is
.I NULL ,
as it is in this example, a random key will be chosen.  The client
may find out the encryption key being used by consulting the
.I ah_key
field of the authentication handle.
.sp
.IP "\fIServer Side\fP"
.LP
The server side is a lot simpler than the client side.  Here is the
previous example rewritten to use
.I AUTH_DES
instead of
.I AUTH_UNIX :
.ie t .DS
.el .DS L
.ft CW
.vs 11
#include <sys/time.h>
#include <rpc/auth_des.h>
	. . .
	. . .
nuser(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	struct authdes_cred *des_cred;
	int uid;
	int gid;
	int gidlen;
	int gidlist[10];
.ft I
	/*
	 * we don't care about authentication for null proc
	 */
.ft CW

	if (rqstp->rq_proc == NULLPROC) {
		/* \fIsame as before\fP */
	}

.ft I
	/*
	 * now get the uid
	 */
.ft CW
	switch (rqstp->rq_cred.oa_flavor) {
	case AUTH_DES:
		des_cred =
			(struct authdes_cred *) rqstp->rq_clntcred;
		if (! netname2user(des_cred->adc_fullname.name,
			&uid, &gid, &gidlen, gidlist))
		{
			fprintf(stderr, "unknown user: %s\en",
				des_cred->adc_fullname.name);
			svcerr_systemerr(transp);
			return;
		}
		break;
	case AUTH_NULL:
	default:
		svcerr_weakauth(transp);
		return;
	}

.ft I
	/*
	 * The rest is the same as before
 	 */
.ft CW
.vs
.DE
Note the use of the routine
.I netname2user (),
the inverse of
.I user2netname ():
it takes a network ID and converts to a unix ID.
.I netname2user ()
also supplies the group IDs which we don't use in this example,
but which may be useful to other UNIX programs.
.NH 2
\&Using Inetd
.IX inetd "" "using \fIinetd\fP"
.LP
An RPC server can be started from
.I inetd
The only difference from the usual code is that the service
creation routine should be called in the following form:
.ie t .DS
.el .DS L
.ft CW
transp = svcudp_create(0);     /* \fIFor UDP\fP */
transp = svctcp_create(0,0,0); /* \fIFor listener TCP sockets\fP */
transp = svcfd_create(0,0,0);  /* \fIFor connected TCP sockets\fP */
.DE
since
.I inet
passes a socket as file descriptor 0.
Also,
.I svc_register()
should be called as
.ie t .DS
.el .DS L
.ft CW
svc_register(transp, PROGNUM, VERSNUM, service, 0);
.DE
with the final flag as 0,
since the program would already be registered by
.I inetd
Remember that if you want to exit
from the server process and return control to
.I inet
you need to explicitly exit, since
.I svc_run()
never returns.
.LP
The format of entries in
.I /etc/inetd.conf
for RPC services is in one of the following two forms:
.ie t .DS
.el .DS L
.ft CW
p_name/version dgram  rpc/udp wait/nowait user server args
p_name/version stream rpc/tcp wait/nowait user server args
.DE
where
.I p_name
is the symbolic name of the program as it appears in
.I rpc(5) ,
.I server
is the program implementing the server,
and
.I program
and
.I version
are the program and version numbers of the service.
For more information, see
.I inetd.conf(5) .
.LP
If the same program handles multiple versions,
then the version number can be a range,
as in this example:
.ie t .DS
.el .DS L
.ft CW
rstatd/1-2 dgram rpc/udp wait root /usr/etc/rpc.rstatd
.DE
.NH 1
\&More Examples
.sp 1
.NH 2
\&Versions
.IX "versions"
.IX "RPC" "versions"
.LP
By convention, the first version number of program
.I PROG
is
.I PROGVERS_ORIG
and the most recent version is
.I PROGVERS
Suppose there is a new version of the
.I user
program that returns an
.I "unsigned short"
rather than a
.I long .
If we name this version
.I RUSERSVERS_SHORT
then a server that wants to support both versions
would do a double register.
.ie t .DS
.el .DS L
.ft CW
if (!svc_register(transp, RUSERSPROG, RUSERSVERS_ORIG,
  nuser, IPPROTO_TCP)) {
	fprintf(stderr, "can't register RUSER service\en");
	exit(1);
}
if (!svc_register(transp, RUSERSPROG, RUSERSVERS_SHORT,
  nuser, IPPROTO_TCP)) {
	fprintf(stderr, "can't register RUSER service\en");
	exit(1);
}
.DE
Both versions can be handled by the same C procedure:
.ie t .DS
.el .DS L
.ft CW
.vs 11
nuser(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	unsigned long nusers;
	unsigned short nusers2;

	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (!svc_sendreply(transp, xdr_void, 0)) {
			fprintf(stderr, "can't reply to RPC call\en");
            return (1);
		}
		return;
	case RUSERSPROC_NUM:
.ft I
		/*
         * Code here to compute the number of users
         * and assign it to the variable \fInusers\fP
		 */
.ft CW
		nusers2 = nusers;
		switch (rqstp->rq_vers) {
		case RUSERSVERS_ORIG:
            if (!svc_sendreply(transp, xdr_u_long,
		    &nusers)) {
                fprintf(stderr,"can't reply to RPC call\en");
			}
			break;
		case RUSERSVERS_SHORT:
            if (!svc_sendreply(transp, xdr_u_short,
		    &nusers2)) {
                fprintf(stderr,"can't reply to RPC call\en");
			}
			break;
		}
	default:
		svcerr_noproc(transp);
		return;
	}
}
.vs
.DE
.KS
.NH 2
\&TCP
.IX "TCP"
.LP
Here is an example that is essentially
.I rcp.
The initiator of the RPC
.I snd
call takes its standard input and sends it to the server
.I rcv
which prints it on standard output.
The RPC call uses TCP.
This also illustrates an XDR procedure that behaves differently
on serialization than on deserialization.
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * The xdr routine:
 *		on decode, read from wire, write onto fp
 *		on encode, read from fp, write onto wire
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>

xdr_rcp(xdrs, fp)
	XDR *xdrs;
	FILE *fp;
{
	unsigned long size;
	char buf[BUFSIZ], *p;

	if (xdrs->x_op == XDR_FREE)/* nothing to free */
		return 1;
	while (1) {
		if (xdrs->x_op == XDR_ENCODE) {
			if ((size = fread(buf, sizeof(char), BUFSIZ,
			  fp)) == 0 && ferror(fp)) {
				fprintf(stderr, "can't fread\en");
				return (1);
			}
		}
		p = buf;
		if (!xdr_bytes(xdrs, &p, &size, BUFSIZ))
			return 0;
		if (size == 0)
			return 1;
		if (xdrs->x_op == XDR_DECODE) {
			if (fwrite(buf, sizeof(char), size,
			  fp) != size) {
				fprintf(stderr, "can't fwrite\en");
				return (1);
			}
		}
	}
}
.vs
.DE
.KE
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * The sender routines
 */
.ft CW
#include <stdio.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <sys/socket.h>
#include <sys/time.h>

main(argc, argv)
	int argc;
	char **argv;
{
	int xdr_rcp();
	int err;

	if (argc < 2) {
		fprintf(stderr, "usage: %s servername\en", argv[0]);
		exit(-1);
	}
	if ((err = callrpctcp(argv[1], RCPPROG, RCPPROC,
	  RCPVERS, xdr_rcp, stdin, xdr_void, 0) != 0)) {
		clnt_perrno(err);
		fprintf(stderr, "can't make RPC call\en");
		exit(1);
	}
	exit(0);
}

callrpctcp(host, prognum, procnum, versnum,
           inproc, in, outproc, out)
	char *host, *in, *out;
	xdrproc_t inproc, outproc;
{
	struct sockaddr_in server_addr;
	int socket = RPC_ANYSOCK;
	enum clnt_stat clnt_stat;
	struct hostent *hp;
	register CLIENT *client;
	struct timeval total_timeout;

	if ((hp = gethostbyname(host)) == NULL) {
		fprintf(stderr, "can't get addr for '%s'\en", host);
		return (-1);
	}
	bcopy(hp->h_addr, (caddr_t)&server_addr.sin_addr,
		hp->h_length);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port =  0;
	if ((client = clnttcp_create(&server_addr, prognum,
	  versnum, &socket, BUFSIZ, BUFSIZ)) == NULL) {
		perror("rpctcp_create");
		return (-1);
	}
	total_timeout.tv_sec = 20;
	total_timeout.tv_usec = 0;
	clnt_stat = clnt_call(client, procnum,
		inproc, in, outproc, out, total_timeout);
	clnt_destroy(client);
	return (int)clnt_stat;
}
.vs
.DE
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * The receiving routines
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>

main()
{
	register SVCXPRT *transp;
     int rcp_service(), xdr_rcp();

	if ((transp = svctcp_create(RPC_ANYSOCK,
	  BUFSIZ, BUFSIZ)) == NULL) {
		fprintf("svctcp_create: error\en");
		exit(1);
	}
	pmap_unset(RCPPROG, RCPVERS);
	if (!svc_register(transp,
	  RCPPROG, RCPVERS, rcp_service, IPPROTO_TCP)) {
		fprintf(stderr, "svc_register: error\en");
		exit(1);
	}
	svc_run();  /* \fInever returns\fP */
	fprintf(stderr, "svc_run should never return\en");
}

rcp_service(rqstp, transp)
	register struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	switch (rqstp->rq_proc) {
	case NULLPROC:
		if (svc_sendreply(transp, xdr_void, 0) == 0) {
			fprintf(stderr, "err: rcp_service");
			return (1);
		}
		return;
	case RCPPROC_FP:
		if (!svc_getargs(transp, xdr_rcp, stdout)) {
			svcerr_decode(transp);
			return;
		}
		if (!svc_sendreply(transp, xdr_void, 0)) {
			fprintf(stderr, "can't reply\en");
			return;
		}
		return (0);
	default:
		svcerr_noproc(transp);
		return;
	}
}
.vs
.DE
.NH 2
\&Callback Procedures
.IX RPC "callback procedures"
.LP
Occasionally, it is useful to have a server become a client,
and make an RPC call back to the process which is its client.
An example is remote debugging,
where the client is a window system program,
and the server is a debugger running on the remote machine.
Most of the time,
the user clicks a mouse button at the debugging window,
which converts this to a debugger command,
and then makes an RPC call to the server
(where the debugger is actually running),
telling it to execute that command.
However, when the debugger hits a breakpoint, the roles are reversed,
and the debugger wants to make an rpc call to the window program,
so that it can inform the user that a breakpoint has been reached.
.LP
In order to do an RPC callback,
you need a program number to make the RPC call on.
Since this will be a dynamically generated program number,
it should be in the transient range,
.I "0x40000000 - 0x5fffffff" .
The routine
.I gettransient()
returns a valid program number in the transient range,
and registers it with the portmapper.
It only talks to the portmapper running on the same machine as the
.I gettransient()
routine itself.  The call to
.I pmap_set()
is a test and set operation,
in that it indivisibly tests whether a program number
has already been registered,
and if it has not, then reserves it.  On return, the
.I sockp
argument will contain a socket that can be used
as the argument to an
.I svcudp_create()
or
.I svctcp_create()
call.
.ie t .DS
.el .DS L
.ft CW
.vs 11
#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/socket.h>

gettransient(proto, vers, sockp)
	int proto, vers, *sockp;
{
	static int prognum = 0x40000000;
	int s, len, socktype;
	struct sockaddr_in addr;

	switch(proto) {
		case IPPROTO_UDP:
			socktype = SOCK_DGRAM;
			break;
		case IPPROTO_TCP:
			socktype = SOCK_STREAM;
			break;
		default:
			fprintf(stderr, "unknown protocol type\en");
			return 0;
	}
	if (*sockp == RPC_ANYSOCK) {
		if ((s = socket(AF_INET, socktype, 0)) < 0) {
			perror("socket");
			return (0);
		}
		*sockp = s;
	}
	else
		s = *sockp;
	addr.sin_addr.s_addr = 0;
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	len = sizeof(addr);
.ft I
	/*
	 * may be already bound, so don't check for error
	 */
.ft CW
	bind(s, &addr, len);
	if (getsockname(s, &addr, &len)< 0) {
		perror("getsockname");
		return (0);
	}
	while (!pmap_set(prognum++, vers, proto,
		ntohs(addr.sin_port))) continue;
	return (prognum-1);
}
.vs
.DE
.SH
Note:
.I
The call to
.I ntohs()
is necessary to ensure that the port number in
.I "addr.sin_port" ,
which is in
.I network
byte order, is passed in
.I host
byte order (as
.I pmap_set()
expects).  See the
.I byteorder(3N)
man page for more details on the conversion of network
addresses from network to host byte order.
.KS
.LP
The following pair of programs illustrate how to use the
.I gettransient()
routine.
The client makes an RPC call to the server,
passing it a transient program number.
Then the client waits around to receive a callback
from the server at that program number.
The server registers the program
.I EXAMPLEPROG
so that it can receive the RPC call
informing it of the callback program number.
Then at some random time (on receiving an
.I ALRM
signal in this example), it sends a callback RPC call,
using the program number it received earlier.
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * client
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>

int callback();
char hostname[256];

main()
{
	int x, ans, s;
	SVCXPRT *xprt;

	gethostname(hostname, sizeof(hostname));
	s = RPC_ANYSOCK;
	x = gettransient(IPPROTO_UDP, 1, &s);
	fprintf(stderr, "client gets prognum %d\en", x);
	if ((xprt = svcudp_create(s)) == NULL) {
	  fprintf(stderr, "rpc_server: svcudp_create\en");
		exit(1);
	}
.ft I
	/* protocol is 0 - gettransient does registering
	 */
.ft CW
	(void)svc_register(xprt, x, 1, callback, 0);
	ans = callrpc(hostname, EXAMPLEPROG, EXAMPLEVERS,
		EXAMPLEPROC_CALLBACK, xdr_int, &x, xdr_void, 0);
	if ((enum clnt_stat) ans != RPC_SUCCESS) {
		fprintf(stderr, "call: ");
		clnt_perrno(ans);
		fprintf(stderr, "\en");
	}
	svc_run();
	fprintf(stderr, "Error: svc_run shouldn't return\en");
}

callback(rqstp, transp)
	register struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	switch (rqstp->rq_proc) {
		case 0:
			if (!svc_sendreply(transp, xdr_void, 0)) {
				fprintf(stderr, "err: exampleprog\en");
				return (1);
			}
			return (0);
		case 1:
			if (!svc_getargs(transp, xdr_void, 0)) {
				svcerr_decode(transp);
				return (1);
			}
			fprintf(stderr, "client got callback\en");
			if (!svc_sendreply(transp, xdr_void, 0)) {
				fprintf(stderr, "err: exampleprog");
				return (1);
			}
	}
}
.vs
.DE
.KE
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * server
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/signal.h>

char *getnewprog();
char hostname[256];
int docallback();
int pnum;		/* \fIprogram number for callback routine\fP */

main()
{
	gethostname(hostname, sizeof(hostname));
	registerrpc(EXAMPLEPROG, EXAMPLEVERS,
	  EXAMPLEPROC_CALLBACK, getnewprog, xdr_int, xdr_void);
	fprintf(stderr, "server going into svc_run\en");
	signal(SIGALRM, docallback);
	alarm(10);
	svc_run();
	fprintf(stderr, "Error: svc_run shouldn't return\en");
}

char *
getnewprog(pnump)
	char *pnump;
{
	pnum = *(int *)pnump;
	return NULL;
}

docallback()
{
	int ans;

	ans = callrpc(hostname, pnum, 1, 1, xdr_void, 0,
		xdr_void, 0);
	if (ans != 0) {
		fprintf(stderr, "server: ");
		clnt_perrno(ans);
		fprintf(stderr, "\en");
	}
}
.vs
.DE
