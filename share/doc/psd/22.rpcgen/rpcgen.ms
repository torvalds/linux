.\"
.\" Must use  --  tbl -- for this one
.\"
.\" @(#)rpcgen.ms	2.2 88/08/04 4.0 RPCSRC
.\" $FreeBSD$
.\"
.de BT
.if \\n%=1 .tl ''- % -''
..
.ND
.\" prevent excess underlining in nroff
.if n .fp 2 R
.OH '\fBrpcgen\fP Programming Guide''Page %'
.EH 'Page %''\fBrpcgen\fP Programming Guide'
.if \n%=1 .bp
.SH
\&\fBrpcgen\fP Programming Guide
.NH 0
\&The \fBrpcgen\fP Protocol Compiler
.IX rpcgen "" \fIrpcgen\fP "" PAGE MAJOR
.LP
.IX RPC "" "" \fIrpcgen\fP
The details of programming applications to use Remote Procedure Calls 
can be overwhelming.  Perhaps most daunting is the writing of the XDR 
routines necessary to convert procedure arguments and results into 
their network format and vice-versa.  
.LP
Fortunately, 
.I rpcgen(1) 
exists to help programmers write RPC applications simply and directly.
.I rpcgen 
does most of the dirty work, allowing programmers to debug 
the  main  features of their application, instead of requiring them to
spend most of their time debugging their network interface code.
.LP
.I rpcgen 
is a  compiler.  It accepts a remote program interface definition written
in a language, called RPC Language, which is similar to C.  It produces a C
language output which includes stub versions of the client routines, a
server skeleton, XDR filter routines for both parameters and results, and a
header file that contains common definitions. The client stubs interface
with the RPC library and effectively hide the network from their callers.
The server stub similarly hides the network from the server procedures that
are to be invoked by remote clients.
.I rpcgen 's
output files can be compiled and linked in the usual way.  The developer
writes server procedures\(emin any language that observes Sun calling
conventions\(emand links them with the server skeleton produced by
.I rpcgen 
to get an executable server program.  To use a remote program, a programmer
writes an ordinary main program that makes local procedure calls to the 
client stubs produced by
.I rpcgen .
Linking this program with 
.I rpcgen 's
stubs creates an executable program.  (At present the main program must be 
written in C).
.I rpcgen 
options can be used to suppress stub generation and to specify the transport
to be used by the server stub.
.LP
Like all compilers, 
.I rpcgen 
reduces development time
that would otherwise be spent coding and debugging low-level routines.
All compilers, including 
.I rpcgen ,
do this at a small cost in efficiency
and flexibility.  However,   many compilers allow  escape  hatches for
programmers to  mix low-level code with  high-level code. 
.I rpcgen 
is no exception.  In speed-critical applications, hand-written routines 
can be linked with the 
.I rpcgen 
output without any difficulty.  Also, one may proceed by using
.I rpcgen 
output as a starting point, and then rewriting it as necessary.
(If you need a discussion of RPC programming without
.I rpcgen ,
see the
.I "Remote Procedure Call Programming Guide)\.
.NH 1
\&Converting Local Procedures into Remote Procedures
.IX rpcgen "local procedures" \fIrpcgen\fP
.IX rpcgen "remote procedures" \fIrpcgen\fP
.LP
Assume an application that runs on a single machine, one which we want 
to convert to run over the network.  Here we will demonstrate such a 
conversion by way of a simple example\(ema program that prints a 
message to the console:
.ie t .DS
.el .DS L
.ft I
/*
 * printmsg.c: print a message on the console
 */
.ft CW
#include <stdio.h>

main(argc, argv)
	int argc;
	char *argv[];
{
	char *message;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <message>\en", argv[0]);
		exit(1);
	}
	message = argv[1];

	if (!printmessage(message)) {
		fprintf(stderr, "%s: couldn't print your message\en",
			argv[0]);
		exit(1);
	} 
	printf("Message Delivered!\en");
	exit(0);
}
.ft I
/*
 * Print a message to the console.
 * Return a boolean indicating whether the message was actually printed.
 */
.ft CW
printmessage(msg)
	char *msg;
{
	FILE *f;

	f = fopen("/dev/console", "w");
	if (f == NULL) {
		return (0);
	}
	fprintf(f, "%s\en", msg);
	fclose(f);
	return(1);
}
.DE
.LP
And then, of course:
.ie t .DS
.el .DS L
.ft CW
example%  \fBcc printmsg.c -o printmsg\fP
example%  \fBprintmsg "Hello, there."\fP
Message delivered!
example%
.DE
.LP
If  
.I printmessage() 
was turned into  a remote procedure,
then it could be  called from anywhere in   the network.  
Ideally,  one would just  like to stick   a  keyword like  
.I remote 
in  front  of a
procedure to turn it into a  remote procedure.  Unfortunately,
we  have to live  within the  constraints of  the   C language, since 
it existed   long before  RPC did.  But   even without language 
support, it's not very difficult to make a procedure remote.
.LP
In  general, it's necessary to figure  out  what the types are for
all procedure inputs and outputs.  In  this case,   we  have a 
procedure
.I printmessage() 
which takes a  string as input, and returns  an integer
as output.  Knowing  this, we can write a  protocol specification in RPC
language that  describes the remote  version of 
.I printmessage ().
Here it is:
.ie t .DS
.el .DS L
.ft I
/*
 * msg.x: Remote message printing protocol
 */
.ft CW

program MESSAGEPROG {
	version MESSAGEVERS {
		int PRINTMESSAGE(string) = 1;
	} = 1;
} = 99;
.DE
.LP
Remote procedures are part of remote programs, so we actually declared
an  entire  remote program  here  which contains  the single procedure
.I PRINTMESSAGE .
This procedure was declared to be  in version  1 of the
remote program.  No null procedure (procedure 0) is necessary because
.I rpcgen 
generates it automatically.
.LP
Notice that everything is declared with all capital  letters.  This is
not required, but is a good convention to follow.
.LP
Notice also that the argument type is \*Qstring\*U and not \*Qchar *\*U.  This
is because a \*Qchar *\*U in C is ambiguous.  Programmers usually intend it
to mean  a null-terminated string   of characters, but  it  could also
represent a pointer to a single character or a  pointer to an array of
characters.  In  RPC language,  a  null-terminated  string is 
unambiguously called a \*Qstring\*U.
.LP
There are  just two more things to  write.  First, there is the remote
procedure itself.  Here's the definition of a remote procedure
to implement the
.I PRINTMESSAGE
procedure we declared above:
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * msg_proc.c: implementation of the remote procedure "printmessage"
 */
.ft CW

#include <stdio.h>
#include <rpc/rpc.h>    /* \fIalways needed\fP  */
#include "msg.h"        /* \fIneed this too: msg.h will be generated by rpcgen\fP */

.ft I
/*
 * Remote verson of "printmessage"
 */
.ft CW
int *
printmessage_1(msg)
	char **msg;
{
	static int result;  /* \fImust be static!\fP */
	FILE *f;

	f = fopen("/dev/console", "w");
	if (f == NULL) {
		result = 0;
		return (&result);
	}
	fprintf(f, "%s\en", *msg);
	fclose(f);
	result = 1;
	return (&result);
}
.vs
.DE
.LP
Notice here that the declaration of the remote procedure
.I printmessage_1() 
differs from that of the local procedure
.I printmessage() 
in three ways:
.IP  1.
It takes a pointer to a string instead of a string itself.  This
is true of all  remote procedures:  they always take pointers to  their
arguments rather than the arguments themselves.
.IP  2.
It returns a pointer to an  integer instead of  an integer itself. This is
also generally true of remote procedures: they always return a pointer
to their results.
.IP  3.
It has an \*Q_1\*U appended to its name.  In general, all remote
procedures called by 
.I rpcgen 
are named by  the following rule: the name in the program  definition  
(here 
.I PRINTMESSAGE )
is converted   to all
lower-case letters, an underbar (\*Q_\*U) is appended to it, and
finally the version number (here 1) is appended.
.LP
The last thing to do is declare the main client program that will call
the remote procedure. Here it is:
.ie t .DS
.el .DS L
.ft I
/*
 * rprintmsg.c: remote version of "printmsg.c"
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>     /* \fIalways needed\fP  */
#include "msg.h"         /* \fIneed this too: msg.h will be generated by rpcgen\fP */

main(argc, argv)
	int argc;
	char *argv[];
{
	CLIENT *cl;
	int *result;
	char *server;
	char *message;

	if (argc < 3) {
		fprintf(stderr, "usage: %s host message\en", argv[0]);
		exit(1);
	}

.ft I
	/*
	 * Save values of command line arguments 
	 */
.ft CW
	server = argv[1];
	message = argv[2];

.ft I
	/*
	 * Create client "handle" used for calling \fIMESSAGEPROG\fP on the
	 * server designated on the command line. We tell the RPC package
	 * to use the "tcp" protocol when contacting the server.
	 */
.ft CW
	cl = clnt_create(server, MESSAGEPROG, MESSAGEVERS, "tcp");
	if (cl == NULL) {
.ft I
		/*
		 * Couldn't establish connection with server.
		 * Print error message and die.
		 */
.ft CW
		clnt_pcreateerror(server);
		exit(1);
	}

.ft I
	/*
	 * Call the remote procedure "printmessage" on the server
	 */
.ft CW
	result = printmessage_1(&message, cl);
	if (result == NULL) {
.ft I
		/*
		 * An error occurred while calling the server. 
	 	 * Print error message and die.
		 */
.ft CW
		clnt_perror(cl, server);
		exit(1);
	}

.ft I
	/*
	 * Okay, we successfully called the remote procedure.
	 */
.ft CW
	if (*result == 0) {
.ft I
		/*
		 * Server was unable to print our message. 
		 * Print error message and die.
		 */
.ft CW
		fprintf(stderr, "%s: %s couldn't print your message\en", 
			argv[0], server);	
		exit(1);
	} 

.ft I
	/*
	 * The message got printed on the server's console
	 */
.ft CW
	printf("Message delivered to %s!\en", server);
}
.DE
There are two things to note here:
.IP  1.
.IX "client handle, used by rpcgen" "" "client handle, used by \fIrpcgen\fP"
First a client \*Qhandle\*U is created using the RPC library routine
.I clnt_create ().
This client handle will be passed  to the stub routines
which call the remote procedure.
.IP  2.
The remote procedure  
.I printmessage_1() 
is called exactly  the same way as it is  declared in 
.I msg_proc.c 
except for the inserted client handle as the first argument.
.LP
Here's how to put all of the pieces together:
.ie t .DS
.el .DS L
.ft CW
example%  \fBrpcgen msg.x\fP
example%  \fBcc rprintmsg.c msg_clnt.c -o rprintmsg\fP
example%  \fBcc msg_proc.c msg_svc.c -o msg_server\fP
.DE
Two programs were compiled here: the client program 
.I rprintmsg 
and the server  program 
.I msg_server .
Before doing this  though,  
.I rpcgen 
was used to fill in the missing pieces.  
.LP
Here is what 
.I rpcgen 
did with the input file 
.I msg.x :
.IP  1.
It created a header file called 
.I msg.h 
that contained
.I #define 's
for
.I MESSAGEPROG ,
.I MESSAGEVERS 
and    
.I PRINTMESSAGE 
for use in  the  other modules.
.IP  2.
It created client \*Qstub\*U routines in the
.I msg_clnt.c 
file.   In this case there is only one, the 
.I printmessage_1() 
that was referred to from the
.I printmsg 
client program.  The name  of the output file for
client stub routines is always formed in this way:  if the name of the
input file is  
.I FOO.x ,
the   client  stubs   output file is    called
.I FOO_clnt.c .
.IP  3.
It created  the  server   program which calls   
.I printmessage_1() 
in
.I msg_proc.c .
This server program is named  
.I msg_svc.c .
The rule for naming the server output file is similar  to the 
previous one:  for an input  file   called  
.I FOO.x ,
the   output   server   file is  named
.I FOO_svc.c .
.LP
Now we're ready to have some fun.  First, copy the server to a
remote machine and run it.  For this  example,  the
machine is called \*Qmoon\*U.  Server processes are run in the
background, because they never exit.
.ie t .DS
.el .DS L
.ft CW
moon% \fBmsg_server &\fP	       
.DE
Then on our local machine (\*Qsun\*U) we can print a message on \*Qmoon\*Us
console.
.ie t .DS
.el .DS L
.ft CW
sun% \fBprintmsg moon "Hello, moon."\fP
.DE
The message will get printed to \*Qmoon\*Us console.  You can print a
message on anybody's console (including your own) with this program if
you are able to copy the server to their machine and run it.
.NH 1
\&Generating XDR Routines
.IX RPC "generating XDR routines"
.LP
The previous example  only demonstrated  the  automatic generation of
client  and server RPC  code. 
.I rpcgen 
may also  be used to generate XDR routines, that  is,  the routines
necessary to  convert   local  data
structures into network format and vice-versa.  This example presents
a complete RPC service\(ema remote directory listing service, which uses
.I rpcgen
not  only  to generate stub routines, but also to  generate  the XDR
routines.  Here is the protocol description file:
.ie t .DS
.el .DS L
.ft I
/*
 * dir.x: Remote directory listing protocol
 */
.ft CW
const MAXNAMELEN = 255;		/* \fImaximum length of a directory entry\fP */

typedef string nametype<MAXNAMELEN>;	/* \fIa directory entry\fP */

typedef struct namenode *namelist;		/* \fIa link in the listing\fP */

.ft I
/*
 * A node in the directory listing
 */
.ft CW
struct namenode {
	nametype name;		/* \fIname of directory entry\fP */
	namelist next;		/* \fInext entry\fP */
};

.ft I
/*
 * The result of a READDIR operation.
 */
.ft CW
union readdir_res switch (int errno) {
case 0:
	namelist list;	/* \fIno error: return directory listing\fP */
default:
	void;		/* \fIerror occurred: nothing else to return\fP */
};

.ft I
/*
 * The directory program definition
 */
.ft CW
program DIRPROG {
	version DIRVERS {
		readdir_res
		READDIR(nametype) = 1;
	} = 1;
} = 76;
.DE
.SH
Note:
.I
Types (like
.I readdir_res 
in the example above) can be defined using
the \*Qstruct\*U, \*Qunion\*U and \*Qenum\*U keywords, but those keywords
should not be used in subsequent declarations of variables of those types.
For example, if you define a union \*Qfoo\*U, you should declare using
only \*Qfoo\*U and not \*Qunion foo\*U.  In fact,
.I rpcgen 
compiles
RPC unions into C structures and it is an error to declare them using the
\*Qunion\*U keyword.
.LP
Running 
.I rpcgen 
on 
.I dir.x 
creates four output files.  Three are the same as before: header file,
client stub routines and server skeleton.  The fourth are the XDR routines
necessary for converting the data types we declared into XDR format and
vice-versa.  These are output in the file
.I dir_xdr.c .
.LP
Here is the implementation of the
.I READDIR 
procedure.
.ie t .DS
.el .DS L
.vs 11
.ft I
/*
 * dir_proc.c: remote readdir implementation
 */
.ft CW
#include <rpc/rpc.h>
#include <sys/dir.h>
#include "dir.h"

extern int errno;
extern char *malloc();
extern char *strdup();

readdir_res *
readdir_1(dirname)
	nametype *dirname;
{
	DIR *dirp;
	struct direct *d;
	namelist nl;
	namelist *nlp;
	static readdir_res res; /* \fImust be static\fP! */

.ft I
	/*
	 * Open directory
	 */
.ft CW
	dirp = opendir(*dirname);
	if (dirp == NULL) {
		res.errno = errno;
		return (&res);
	}

.ft I
	/*
	 * Free previous result
	 */
.ft CW
	xdr_free(xdr_readdir_res, &res);

.ft I
	/*
	 * Collect directory entries.
	 * Memory allocated here will be freed by \fIxdr_free\fP
	 * next time \fIreaddir_1\fP is called
	 */
.ft CW
	nlp = &res.readdir_res_u.list;
	while (d = readdir(dirp)) {
		nl = *nlp = (namenode *) malloc(sizeof(namenode));
		nl->name = strdup(d->d_name);
		nlp = &nl->next;
	}
	*nlp = NULL;

.ft I
	/*
	 * Return the result
	 */
.ft CW
	res.errno = 0;
	closedir(dirp);
	return (&res);
}
.vs
.DE
Finally, there is the client side program to call the server:
.ie t .DS
.el .DS L
.ft I
/*
 * rls.c: Remote directory listing client
 */
.ft CW
#include <stdio.h>
#include <rpc/rpc.h>	/* \fIalways need this\fP */
#include "dir.h"		/* \fIwill be generated by rpcgen\fP */

extern int errno;

main(argc, argv)
	int argc;
	char *argv[];
{
	CLIENT *cl;
	char *server;
	char *dir;
	readdir_res *result;
	namelist nl;


	if (argc != 3) {
		fprintf(stderr, "usage: %s host directory\en", 
		  argv[0]);
		exit(1);
	}

.ft I
	/*
	 * Remember what our command line arguments refer to
	 */
.ft CW
	server = argv[1];
	dir = argv[2];

.ft I
	/*
	 * Create client "handle" used for calling \fIMESSAGEPROG\fP on the
	 * server designated on the command line. We tell the RPC package
	 * to use the "tcp" protocol when contacting the server.
	 */
.ft CW
	cl = clnt_create(server, DIRPROG, DIRVERS, "tcp");
	if (cl == NULL) {
.ft I
		/*
		 * Couldn't establish connection with server.
		 * Print error message and die.
		 */
.ft CW
		clnt_pcreateerror(server);
		exit(1);
	}

.ft I
	/*
	 * Call the remote procedure \fIreaddir\fP on the server
	 */
.ft CW
	result = readdir_1(&dir, cl);
	if (result == NULL) {
.ft I
		/*
		 * An error occurred while calling the server. 
	 	 * Print error message and die.
		 */
.ft CW
		clnt_perror(cl, server);
		exit(1);
	}

.ft I
	/*
	 * Okay, we successfully called the remote procedure.
	 */
.ft CW
	if (result->errno != 0) {
.ft I
		/*
		 * A remote system error occurred.
		 * Print error message and die.
		 */
.ft CW
		errno = result->errno;
		perror(dir);
		exit(1);
	}

.ft I
	/*
	 * Successfully got a directory listing.
	 * Print it out.
	 */
.ft CW
	for (nl = result->readdir_res_u.list; nl != NULL; 
	  nl = nl->next) {
		printf("%s\en", nl->name);
	}
	exit(0);
}
.DE
Compile everything, and run.
.DS
.ft CW
sun%  \fBrpcgen dir.x\fP
sun%  \fBcc rls.c dir_clnt.c dir_xdr.c -o rls\fP
sun%  \fBcc dir_svc.c dir_proc.c dir_xdr.c -o dir_svc\fP

sun%  \fBdir_svc &\fP

moon%  \fBrls sun /usr/pub\fP
\&.
\&..
ascii
eqnchar
greek
kbd
marg8
tabclr
tabs
tabs4
moon%
.DE
.LP
.IX "debugging with rpcgen" "" "debugging with \fIrpcgen\fP"
A final note about 
.I rpcgen :
The client program and the server procedure can be tested together 
as a single program by simply linking them with each other rather 
than with the client and server stubs.  The procedure calls will be
executed as ordinary local procedure calls and the program can be 
debugged with a local debugger such as 
.I dbx .
When the program is working, the client program can be linked to 
the client stub produced by 
.I rpcgen 
and the server procedures can be linked to the server stub produced
by 
.I rpcgen .
.SH
.I NOTE :
\fIIf you do this, you may want to comment out calls to RPC library
routines, and have client-side routines call server routines
directly.\fP
.LP
.NH 1
\&The C-Preprocessor
.IX rpcgen "C-preprocessor" \fIrpcgen\fP
.LP
The C-preprocessor is  run on all input  files before they are
compiled, so all the preprocessor directives are legal within a \*Q.x\*U
file. Four symbols may be defined, depending upon which output file is
getting generated. The symbols are:
.TS
box tab (&);
lfI lfI
lfL l .
Symbol&Usage
_
RPC_HDR&for header-file output
RPC_XDR&for XDR routine output
RPC_SVC&for server-skeleton output
RPC_CLNT&for client stub output
.TE
.LP
Also, 
.I rpcgen 
does  a little preprocessing   of its own. Any  line that
begins  with  a percent sign is passed  directly into the output file,
without any interpretation of the line.  Here is a simple example that
demonstrates the preprocessing features.
.ie t .DS
.el .DS L
.ft I
/*
 * time.x: Remote time protocol
 */
.ft CW
program TIMEPROG {
        version TIMEVERS {
                unsigned int TIMEGET(void) = 1;
        } = 1;
} = 44;

#ifdef RPC_SVC
%int *
%timeget_1()
%{
%        static int thetime;
%
%        thetime = time(0);
%        return (&thetime);
%}
#endif
.DE
The '%' feature is not generally recommended, as there is no guarantee
that the compiler will stick the output where you intended.
.NH 1
\&\fBrpcgen\fP Programming Notes
.IX rpcgen "other operations" \fIrpcgen\fP
.sp 
.NH 2
\&Timeout Changes
.IX rpcgen "timeout changes" \fIrpcgen\fP
.LP
RPC sets a default timeout of 25 seconds for RPC calls when
.I clnt_create()
is used.  This timeout may be changed using
.I clnt_control()
Here is a small code fragment to demonstrate use of
.I clnt_control ():
.ID
struct timeval tv;
CLIENT *cl;
.sp .5
cl = clnt_create("somehost", SOMEPROG, SOMEVERS, "tcp");
if (cl == NULL) {
	exit(1);
}
tv.tv_sec = 60;	/* \fIchange timeout to 1 minute\fP */
tv.tv_usec = 0;
clnt_control(cl, CLSET_TIMEOUT, &tv);	
.DE
.NH 2
\&Handling Broadcast on the Server Side
.IX "broadcast RPC"
.IX rpcgen "broadcast RPC" \fIrpcgen\fP
.LP
When a procedure is known to be called via broadcast RPC,
it is usually wise for the server to not reply unless it can provide
some useful information to the client.  This prevents the network
from getting flooded by useless replies.
.LP
To prevent the server from replying, a remote procedure can
return NULL as its result, and the server code generated by
.I rpcgen 
will detect this and not send out a reply.
.LP
Here is an example of a procedure that replies only if it
thinks it is an NFS server:
.ID
void *
reply_if_nfsserver()
{
	char notnull;	/* \fIjust here so we can use its address\fP */
.sp .5
	if (access("/etc/exports", F_OK) < 0) {
		return (NULL);	/* \fIprevent RPC from replying\fP */
	}
.ft I
	/*
	 * return non-null pointer so RPC will send out a reply
	 */
.ft L
	return ((void *)&notnull);
}
.DE
Note that if procedure returns type \*Qvoid *\*U, they must return a non-NULL
pointer if they want RPC to reply for them.
.NH 2
\&Other Information Passed to Server Procedures
.LP
Server procedures will often want to know more about an RPC call
than just its arguments.  For example, getting authentication information
is important to procedures that want to implement some level of security.
This extra information is actually supplied to the server procedure as a
second argument.  Here is an example to demonstrate its use.  What we've
done here is rewrite the previous
.I printmessage_1() 
procedure to only allow root users to print a message to the console.
.ID
int *
printmessage_1(msg, rq)
	char **msg;
	struct svc_req	*rq;
{
	static in result;	/* \fIMust be static\fP */
	FILE *f;
	struct suthunix_parms *aup;
.sp .5
	aup = (struct authunix_parms *)rq->rq_clntcred;
	if (aup->aup_uid != 0) {
		result = 0;
		return (&result);
	}
.sp
.ft I
	/*
	 * Same code as before.
	 */
.ft L
}
.DE
.NH 1
\&RPC Language
.IX RPCL
.IX rpcgen "RPC Language" \fIrpcgen\fP
.LP
RPC language is an extension of XDR  language.   The sole extension is
the addition of the
.I program 
type.  For a complete description of the XDR language syntax, see the
.I "External Data Representation Standard: Protocol Specification"
chapter.  For a description of the RPC extensions to the XDR language,
see the
.I "Remote Procedure Calls: Protocol Specification"
chapter.
.LP
However, XDR language is so close to C that if you know C, you know most
of it already.  We describe here  the syntax of the RPC language,
showing a  few examples along the way.   We also show how  the various
RPC and XDR type definitions get  compiled into C  type definitions in
the output header file.
.KS
.NH 2
Definitions
\&
.IX rpcgen definitions \fIrpcgen\fP
.LP
An RPC language file consists of a series of definitions.
.DS L
.ft CW
    definition-list:
        definition ";"
        definition ";" definition-list
.DE
.KE
It recognizes five types of definitions. 
.DS L
.ft CW
    definition:
        enum-definition
        struct-definition
        union-definition
        typedef-definition
        const-definition
        program-definition
.DE
.NH 2
Structures
\&
.IX rpcgen structures \fIrpcgen\fP
.LP
An XDR struct  is declared almost exactly like  its C counterpart.  It
looks like the following:
.DS L
.ft CW
    struct-definition:
        "struct" struct-ident "{"
            declaration-list
        "}"

    declaration-list:
        declaration ";"
        declaration ";" declaration-list
.DE
As an example, here is an XDR structure to a two-dimensional
coordinate, and the C structure  that it  gets compiled into  in the
output header file.
.DS
.ft CW
   struct coord {             struct coord {
        int x;       -->           int x;
        int y;                     int y;
   };                         };
                              typedef struct coord coord;
.DE
The output is identical to the  input, except  for the added
.I typedef
at the end of the output.  This allows one to use \*Qcoord\*U instead of
\*Qstruct coord\*U when declaring items.
.NH 2
Unions
\&
.IX rpcgen unions \fIrpcgen\fP
.LP
XDR unions are discriminated unions, and look quite different from C
unions. They are more analogous to  Pascal variant records than they
are to C unions.
.DS L
.ft CW
    union-definition:
        "union" union-ident "switch" "(" declaration ")" "{"
            case-list
        "}"

    case-list:
        "case" value ":" declaration ";"
        "default" ":" declaration ";"
        "case" value ":" declaration ";" case-list
.DE
Here is an example of a type that might be returned as the result of a
\*Qread data\*U operation.  If there is no error, return a block of data.
Otherwise, don't return anything.
.DS L
.ft CW
    union read_result switch (int errno) {
    case 0:
        opaque data[1024];
    default:
        void;
    };
.DE
It gets compiled into the following:
.DS L
.ft CW
    struct read_result {
        int errno;
        union {
            char data[1024];
        } read_result_u;
    };
    typedef struct read_result read_result;
.DE
Notice that the union component of the  output struct  has the name as
the type name, except for the trailing \*Q_u\*U.
.NH 2
Enumerations
\&
.IX rpcgen enumerations \fIrpcgen\fP
.LP
XDR enumerations have the same syntax as C enumerations.
.DS L
.ft CW
    enum-definition:
        "enum" enum-ident "{"
            enum-value-list
        "}"

    enum-value-list:
        enum-value
        enum-value "," enum-value-list

    enum-value:
        enum-value-ident 
        enum-value-ident "=" value
.DE
Here is a short example of  an XDR enum,  and the C enum that  it gets
compiled into.
.DS L
.ft CW
     enum colortype {      enum colortype {
          RED = 0,              RED = 0,
          GREEN = 1,   -->      GREEN = 1,
          BLUE = 2              BLUE = 2,
     };                    };
                           typedef enum colortype colortype;
.DE
.NH 2
Typedef
\&
.IX rpcgen typedef \fIrpcgen\fP
.LP
XDR typedefs have the same syntax as C typedefs.
.DS L
.ft CW
    typedef-definition:
        "typedef" declaration
.DE
Here  is an example  that defines a  
.I fname_type 
used  for declaring
file name strings that have a maximum length of 255 characters.
.DS L
.ft CW
typedef string fname_type<255>; --> typedef char *fname_type;
.DE
.NH 2
Constants
\&
.IX rpcgen constants \fIrpcgen\fP
.LP
XDR constants  symbolic constants  that may be  used wherever  a
integer constant is used, for example, in array size specifications.
.DS L
.ft CW
    const-definition:
        "const" const-ident "=" integer
.DE
For example, the following defines a constant
.I DOZEN 
equal to 12.
.DS L
.ft CW
    const DOZEN = 12;  -->  #define DOZEN 12
.DE
.NH 2
Programs
\&
.IX rpcgen programs \fIrpcgen\fP
.LP
RPC programs are declared using the following syntax:
.DS L
.ft CW
    program-definition:
        "program" program-ident "{" 
            version-list
        "}" "=" value 

    version-list:
        version ";"
        version ";" version-list

    version:
        "version" version-ident "{"
            procedure-list 
        "}" "=" value

    procedure-list:
        procedure ";"
        procedure ";" procedure-list

    procedure:
        type-ident procedure-ident "(" type-ident ")" "=" value
.DE
For example, here is the time protocol, revisited:
.ie t .DS
.el .DS L
.ft I
/*
 * time.x: Get or set the time. Time is represented as number of seconds
 * since 0:00, January 1, 1970.
 */
.ft CW
program TIMEPROG {
    version TIMEVERS {
        unsigned int TIMEGET(void) = 1;
        void TIMESET(unsigned) = 2;
    } = 1;
} = 44;        
.DE
This file compiles into #defines in the output header file:
.ie t .DS
.el .DS L
.ft CW
#define TIMEPROG 44
#define TIMEVERS 1
#define TIMEGET 1
#define TIMESET 2
.DE
.NH 2
Declarations
\&
.IX rpcgen declarations \fIrpcgen\fP
.LP
In XDR, there are only four kinds of declarations.  
.DS L
.ft CW
    declaration:
        simple-declaration
        fixed-array-declaration
        variable-array-declaration
        pointer-declaration
.DE
\fB1) Simple declarations\fP are just like simple C declarations.
.DS L
.ft CW
    simple-declaration:
        type-ident variable-ident
.DE
Example:
.DS L
.ft CW
    colortype color;    --> colortype color;
.DE
\fB2) Fixed-length Array Declarations\fP are just like C array declarations:
.DS L
.ft CW
    fixed-array-declaration:
        type-ident variable-ident "[" value "]"
.DE
Example:
.DS L
.ft CW
    colortype palette[8];    --> colortype palette[8];
.DE
\fB3) Variable-Length Array Declarations\fP have no explicit syntax 
in C, so XDR invents its own using angle-brackets.
.DS L
.ft CW
variable-array-declaration:
    type-ident variable-ident "<" value ">"
    type-ident variable-ident "<" ">"
.DE
The maximum size is specified between the angle brackets. The size may
be omitted, indicating that the array may be of any size.
.DS L
.ft CW
    int heights<12>;    /* \fIat most 12 items\fP */
    int widths<>;       /* \fIany number of items\fP */
.DE
Since  variable-length  arrays have no  explicit  syntax in  C,  these
declarations are actually compiled into \*Qstruct\*Us.  For example, the
\*Qheights\*U declaration gets compiled into the following struct:
.DS L
.ft CW
    struct {
        u_int heights_len;  /* \fI# of items in array\fP */
        int *heights_val;   /* \fIpointer to array\fP */
    } heights;
.DE
Note that the number of items in the array is stored in the \*Q_len\*U
component and the pointer to the array is stored in the \*Q_val\*U
component. The first part of each of these component's names is the
same as the name of the declared XDR variable.
.LP
\fB4) Pointer Declarations\fP are made in 
XDR  exactly as they  are  in C.  You  can't
really send pointers over the network,  but  you  can use XDR pointers
for sending recursive data types such as lists and trees.  The type is
actually called \*Qoptional-data\*U, not \*Qpointer\*U, in XDR language.
.DS L
.ft CW
    pointer-declaration:
        type-ident "*" variable-ident
.DE
Example:
.DS L
.ft CW
    listitem *next;  -->  listitem *next;
.DE
.NH 2
\&Special Cases
.IX rpcgen "special cases" \fIrpcgen\fP
.LP
There are a few exceptions to the rules described above.
.LP
.B Booleans:
C has no built-in boolean type. However, the RPC library does  a
boolean type   called 
.I bool_t 
that   is either  
.I TRUE 
or  
.I FALSE .
Things declared as  type 
.I bool 
in  XDR language  are  compiled  into
.I bool_t 
in the output header file.
.LP
Example:
.DS L
.ft CW
    bool married;  -->  bool_t married;
.DE
.B Strings:
C has  no built-in string  type, but  instead uses the null-terminated
\*Qchar *\*U convention.  In XDR language, strings are declared using the
\*Qstring\*U keyword, and compiled into \*Qchar *\*Us in the output header
file. The  maximum size contained  in the angle brackets specifies the
maximum number of characters allowed in the  strings (not counting the
.I NULL 
character). The maximum size may be left off, indicating a string
of arbitrary length.
.LP
Examples:
.DS L
.ft CW
    string name<32>;    -->  char *name;
    string longname<>;  -->  char *longname;
.DE
.B "Opaque  Data:"
Opaque data is used in RPC and XDR to describe untyped  data, that is,
just  sequences of arbitrary  bytes.  It may be  declared  either as a
fixed or variable length array.
.DS L
Examples:
.ft CW
    opaque diskblock[512];  -->  char diskblock[512];

    opaque filedata<1024>;  -->  struct {
                                    u_int filedata_len;
                                    char *filedata_val;
                                 } filedata;
.DE
.B Voids:
In a void declaration, the variable is  not named.  The declaration is
just \*Qvoid\*U and nothing else.  Void declarations can only occur in two
places: union definitions and program definitions (as the  argument or
result of a remote procedure).
