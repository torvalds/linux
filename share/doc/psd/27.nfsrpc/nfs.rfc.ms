.\"
.\" Must use  --  tbl  --  with this one
.\"
.\" @(#)nfs.rfc.ms	2.2 88/08/05 4.0 RPCSRC
.\" $FreeBSD$
.\"
.de BT
.if \\n%=1 .tl ''- % -''
..
.ND
.\" prevent excess underlining in nroff
.if n .fp 2 R
.OH 'Network File System: Version 2 Protocol Specification''Page %'
.EH 'Page %''Network File System: Version 2 Protocol Specification'
.if \n%=1 .bp
.SH
\&Network File System: Version 2 Protocol Specification
.IX NFS "" "" "" PAGE MAJOR
.IX "Network File System" "" "" "" PAGE MAJOR
.IX NFS "version-2 protocol specification"
.IX "Network File System" "version-2 protocol specification"
.LP
.NH 0
\&Status of this Standard
.LP
Note: This document specifies a protocol that Sun Microsystems, Inc.,
and others are using.  It specifies it in standard ARPA RFC form.
.NH 1
\&Introduction
.IX NFS introduction
.LP
The Sun Network Filesystem (NFS) protocol provides transparent remote 
access to shared filesystems over local area networks.  The NFS 
protocol is designed to be machine, operating system, network architecture, 
and transport protocol independent.  This independence is 
achieved through the use of Remote Procedure Call (RPC) primitives 
built on top of an External Data Representation (XDR).  Implementations
exist for a variety of machines, from personal computers to
supercomputers.
.LP
The supporting mount protocol allows the server to hand out remote
access privileges to a restricted set of clients.  It performs the
operating system-specific functions that allow, for example, to
attach remote directory trees to some local file system.
.NH 2
\&Remote Procedure Call
.IX "Remote Procedure Call"
.LP
Sun's remote procedure call specification provides a procedure-
oriented interface to remote services.  Each server supplies a
program that is a set of procedures.  NFS is one such "program".
The combination of host address, program number, and procedure
number specifies one remote service procedure.  RPC does not depend
on services provided by specific protocols, so it can be used with
any underlying transport protocol.  See the
.I "Remote Procedure Calls: Protocol Specification"
chapter of this manual.
.NH 2
\&External Data Representation
.IX "External Data Representation"
.LP
The External Data Representation (XDR) standard provides a common
way of representing a set of data types over a network.  
The NFS
Protocol Specification is written using the RPC data description
language.  
For more information, see the
.I " External Data Representation Standard: Protocol Specification."  
Sun provides implementations of XDR and
RPC,  but NFS does not require their use.  Any software that
provides equivalent functionality can be used, and if the encoding
is exactly the same it can interoperate with other implementations
of NFS.
.NH 2
\&Stateless Servers
.IX "stateless servers"
.IX servers stateless
.LP
The NFS protocol is stateless.  That is, a server does not need to
maintain any extra state information about any of its clients in
order to function correctly.  Stateless servers have a distinct
advantage over stateful servers in the event of a failure.  With
stateless servers, a client need only retry a request until the
server responds; it does not even need to know that the server has
crashed, or the network temporarily went down.  The client of a
stateful server, on the other hand, needs to either detect a server
crash and rebuild the server's state when it comes back up, or
cause client operations to fail.
.LP
This may not sound like an important issue, but it affects the
protocol in some unexpected ways.  We feel that it is worth a bit
of extra complexity in the protocol to be able to write very simple
servers that do not require fancy crash recovery.
.LP
On the other hand, NFS deals with objects such as files and
directories that inherently have state -- what good would a file be
if it did not keep its contents intact?  The goal is to not
introduce any extra state in the protocol itself.  Another way to
simplify recovery is by making operations "idempotent" whenever
possible (so that they can potentially be repeated).
.NH 1
\&NFS Protocol Definition
.IX NFS "protocol definition"
.IX NFS protocol
.LP
Servers have been known to change over time, and so can the
protocol that they use. So RPC provides a version number with each
RPC request. This RFC describes version two of the NFS protocol.
Even in the second version, there are various obsolete procedures
and parameters, which will be removed in later versions. An RFC
for version three of the NFS protocol is currently under
preparation.
.NH 2
\&File System Model
.IX filesystem model
.LP
NFS assumes a file system that is hierarchical, with directories as
all but the bottom-level files.  Each entry in a directory (file,
directory, device, etc.)  has a string name.  Different operating
systems may have restrictions on the depth of the tree or the names
used, as well as using different syntax to represent the "pathname",
which is the concatenation of all the "components" (directory and
file names) in the name.  A "file system" is a tree on a single
server (usually a single disk or physical partition) with a specified
"root".  Some operating systems provide a "mount" operation to make
all file systems appear as a single tree, while others maintain a
"forest" of file systems.  Files are unstructured streams of
uninterpreted bytes.  Version 3 of NFS uses a slightly more general
file system model.
.LP
NFS looks up one component of a pathname at a time.  It may not be
obvious why it does not just take the whole pathname, traipse down
the directories, and return a file handle when it is done.  There are
several good reasons not to do this.  First, pathnames need
separators between the directory components, and different operating
systems use different separators.  We could define a Network Standard
Pathname Representation, but then every pathname would have to be
parsed and converted at each end.  Other issues are discussed in
\fINFS Implementation Issues\fP below.
.LP
Although files and directories are similar objects in many ways,
different procedures are used to read directories and files.  This
provides a network standard format for representing directories.  The
same argument as above could have been used to justify a procedure
that returns only one directory entry per call.  The problem is
efficiency.  Directories can contain many entries, and a remote call
to return each would be just too slow.
.NH 2
\&RPC Information
.IX NFS "RPC information"
.IP \fIAuthentication\fP
The   NFS  service uses 
.I AUTH_UNIX ,
.I AUTH_DES ,
or 
.I AUTH_SHORT 
style
authentication, except in  the  NULL procedure where   
.I AUTH_NONE 
is also allowed.
.IP "\fITransport Protocols\fP"
NFS currently is supported on UDP/IP only.  
.IP "\fIPort Number\fP"
The NFS protocol currently uses the UDP port number 2049.  This is
not an officially assigned port, so  later versions of the protocol
use the \*QPortmapping\*U facility of RPC.
.NH 2
\&Sizes of XDR Structures
.IX "XDR structure sizes"
.LP
These are the sizes, given in decimal bytes, of various XDR
structures used in the protocol:
.DS
/* \fIThe maximum number of bytes of data in a READ or WRITE request\fP  */
const MAXDATA = 8192;

/* \fIThe maximum number of bytes in a pathname argument\fP */
const MAXPATHLEN = 1024;

/* \fIThe maximum number of bytes in a file name argument\fP */
const MAXNAMLEN = 255;

/* \fIThe size in bytes of the opaque "cookie" passed by READDIR\fP */
const COOKIESIZE  = 4;

/* \fIThe size in bytes of the opaque file handle\fP */
const FHSIZE = 32;
.DE
.NH 2
\&Basic Data Types
.IX "NFS data types"
.IX NFS "basic data types"
.LP
The following XDR  definitions are basic  structures and types used
in other structures described further on.
.KS
.NH 3
\&stat
.IX "NFS data types" stat "" \fIstat\fP
.DS
enum stat {
	NFS_OK = 0,
	NFSERR_PERM=1,
	NFSERR_NOENT=2,
	NFSERR_IO=5,
	NFSERR_NXIO=6,
	NFSERR_ACCES=13,
	NFSERR_EXIST=17,
	NFSERR_NODEV=19,
	NFSERR_NOTDIR=20,
	NFSERR_ISDIR=21,
	NFSERR_FBIG=27,
	NFSERR_NOSPC=28,
	NFSERR_ROFS=30,
	NFSERR_NAMETOOLONG=63,
	NFSERR_NOTEMPTY=66,
	NFSERR_DQUOT=69,
	NFSERR_STALE=70,
	NFSERR_WFLUSH=99
};
.DE
.KE
.LP
The 
.I stat 
type  is returned with every  procedure's  results.   A
value of 
.I NFS_OK 
indicates that the  call completed successfully and
the  results are  valid.  The  other  values indicate  some kind of
error  occurred on the  server  side  during the servicing   of the
procedure.  The error values are derived from UNIX error numbers.
.IP \fBNFSERR_PERM\fP:
Not owner.  The caller does not have correct ownership
to perform the requested operation.
.IP \fBNFSERR_NOENT\fP:
No such file or directory.    The file or directory
specified does not exist.
.IP \fBNFSERR_IO\fP:
Some sort of hard  error occurred when the operation was
in progress.  This could be a disk error, for example.
.IP \fBNFSERR_NXIO\fP:
No such device or address.
.IP \fBNFSERR_ACCES\fP:
Permission  denied.  The  caller does  not  have the
correct permission to perform the requested operation.
.IP \fBNFSERR_EXIST\fP:
File exists.  The file specified already exists.
.IP \fBNFSERR_NODEV\fP:
No such device.
.IP \fBNFSERR_NOTDIR\fP:
Not   a  directory.    The  caller  specified   a
non-directory in a directory operation.
.IP \fBNFSERR_ISDIR\fP:
Is a directory.  The caller specified  a directory in
a non- directory operation.
.IP \fBNFSERR_FBIG\fP:
File too large.   The  operation caused a file to grow
beyond the server's limit.
.IP \fBNFSERR_NOSPC\fP:
No space left on  device.   The operation caused the
server's filesystem to reach its limit.
.IP \fBNFSERR_ROFS\fP:
Read-only filesystem.  Write attempted on a read-only filesystem.
.IP \fBNFSERR_NAMETOOLONG\fP:
File name   too   long.  The file  name  in  an operation was too long.
.IP \fBNFSERR_NOTEMPTY\fP:
Directory   not empty.  Attempted  to   remove  a
directory that was not empty.
.IP \fBNFSERR_DQUOT\fP:
Disk quota exceeded.  The client's disk  quota on the
server has been exceeded.
.IP \fBNFSERR_STALE\fP:
The  "fhandle" given in   the arguments was invalid.
That is, the file referred to by that file handle no longer exists,
or access to it has been revoked.
.IP \fBNFSERR_WFLUSH\fP:
The server's  write cache  used  in the
.I WRITECACHE 
call got flushed to disk.
.LP
.KS
.NH 3
\&ftype
.IX "NFS data types" ftype "" \fIftype\fP
.DS
enum ftype {
	NFNON = 0,
	NFREG = 1,
	NFDIR = 2,
	NFBLK = 3,
	NFCHR = 4,
	NFLNK = 5
};
.DE
.KE
The enumeration
.I ftype 
gives the type of a file.  The type 
.I NFNON 
indicates a non-file,
.I NFREG 
is a regular file, 
.I NFDIR 
is a directory,
.I NFBLK 
is a block-special device, 
.I NFCHR 
is a character-special device, and
.I NFLNK 
is a symbolic link.
.KS
.NH 3
\&fhandle
.IX "NFS data types" fhandle "" \fIfhandle\fP
.DS
typedef opaque fhandle[FHSIZE];
.DE
.KE
The
.I fhandle 
is the file handle passed between the server and the client.  
All file operations are done using file handles to refer to a file or 
directory.  The file handle can contain whatever information the server
needs to distinguish an individual file.
.KS
.NH 3
\&timeval
.IX "NFS data types" timeval "" \fItimeval\fP
.DS
struct timeval {
	unsigned int seconds;
	unsigned int useconds;
};
.DE
.KE
The 
.I timeval
structure is the number of seconds and microseconds 
since midnight January 1, 1970, Greenwich Mean Time.  It is used to 
pass time and date information.
.KS
.NH 3
\&fattr
.IX "NFS data types" fattr "" \fIfattr\fP
.DS
struct fattr {
	ftype        type;
	unsigned int mode;
	unsigned int nlink;
	unsigned int uid;
	unsigned int gid;
	unsigned int size;
	unsigned int blocksize;
	unsigned int rdev;
	unsigned int blocks;
	unsigned int fsid;
	unsigned int fileid;
	timeval      atime;
	timeval      mtime;
	timeval      ctime;
};
.DE
.KE
The
.I fattr 
structure contains the attributes of a file; "type" is the type of
the file; "nlink" is the number of hard links to the file (the number
of different names for the same file); "uid" is the user
identification number of the owner of the file; "gid" is the group
identification number of the group of the file; "size" is the size in
bytes of the file; "blocksize" is the size in bytes of a block of the
file; "rdev" is the device number of the file if it is type
.I NFCHR 
or
.I NFBLK ;
"blocks" is the number of blocks the file takes up on disk; "fsid" is
the file system identifier for the filesystem containing the file;
"fileid" is a number that uniquely identifies the file within its
filesystem; "atime" is the time when the file was last accessed for
either read or write; "mtime" is the time when the file data was last
modified (written); and "ctime" is the time when the status of the
file was last changed.  Writing to the file also changes "ctime" if
the size of the file changes.
.LP
"mode" is the access mode encoded as a set of bits.  Notice that the
file type is specified both in the mode bits and in the file type.
This is really a bug in the protocol and will be fixed in future
versions.  The descriptions given below specify the bit positions
using octal numbers.
.TS
box tab (&) ;
cfI cfI
lfL l .
Bit&Description
_
0040000&This is a directory; "type" field should be NFDIR.
0020000&This is a character special file; "type" field should be NFCHR. 
0060000&This is a block special file; "type" field should be NFBLK. 
0100000&This is a regular file; "type" field should be NFREG.
0120000&This is a symbolic link file;  "type" field should be NFLNK. 
0140000&This is a named socket; "type" field should be NFNON.
0004000&Set user id on execution.
0002000&Set group id on execution.
0001000&Save swapped text even after use.
0000400&Read permission for owner.
0000200&Write permission for owner.
0000100&Execute and search permission for owner.
0000040&Read permission for group.
0000020&Write permission for group.
0000010&Execute and search permission for group.
0000004&Read permission for others.
0000002&Write permission for others.
0000001&Execute and search permission for others.
.TE
.KS
Notes:
.IP 
The bits are  the same as the mode   bits returned  by  the
.I stat(2) 
system call in the UNIX system.  The file  type is  specified  both in
the mode  bits  and in  the file type.   This   is fixed  in future
versions.
.IP
The "rdev" field in the attributes structure is an operating system
specific device specifier.  It  will be  removed and generalized in
the next revision of the protocol.
.KE
.LP
.KS
.NH 3
\&sattr
.IX "NFS data types" sattr "" \fIsattr\fP
.DS
struct sattr {
	unsigned int mode;
	unsigned int uid;
	unsigned int gid;
	unsigned int size;
	timeval      atime;
	timeval      mtime;
};
.DE
.KE
The 
.I sattr
structure contains the file attributes which can be set
from the client.  The fields are the same as for  
.I fattr 
above.  A "size" of zero  means the file should be  truncated.
A value of -1 indicates a field that should be ignored.
.LP
.KS
.NH 3
\&filename
.IX "NFS data types" filename "" \fIfilename\fP
.DS
typedef string filename<MAXNAMLEN>;
.DE
.KE
The type
.I filename 
is used for  passing file names  or  pathname components.
.LP
.KS
.NH 3
\&path
.IX "NFS data types" path "" \fIpath\fP
.DS
typedef string path<MAXPATHLEN>;
.DE
.KE
The type
.I path 
is a pathname.  The server considers it as a string
with no internal structure,  but to the client  it is the name of a
node in a filesystem tree.
.LP
.KS
.NH 3
\&attrstat
.IX "NFS data types" attrstat "" \fIattrstat\fP
.DS
union attrstat switch (stat status) {
	case NFS_OK:
		fattr attributes;
	default:
		void;
};
.DE
.KE
The 
.I attrstat 
structure is a common procedure result.  It contains
a  "status" and,  if  the call   succeeded,   it also contains  the
attributes of the file on which the operation was done.
.LP
.KS
.NH 3
\&diropargs
.IX "NFS data types" diropargs "" \fIdiropargs\fP
.DS
struct diropargs {
	fhandle  dir;
	filename name;
};
.DE
.KE
The  
.I diropargs 
structure is used  in  directory  operations.  The
"fhandle" "dir" is the directory in  which to find the file "name".
A directory operation is one in which the directory is affected.
.LP
.KS
.NH 3
\&diropres
.IX "NFS data types" diropres "" \fIdiropres\fP
.DS
union diropres switch (stat status) {
	case NFS_OK:
		struct {
			fhandle file;
			fattr   attributes;
		} diropok;
	default:
		void;
};
.DE
.KE
The results of a directory operation  are returned  in a 
.I diropres 
structure.  If the call succeeded, a new file handle "file" and the
"attributes" associated with that file are  returned along with the
"status".
.NH 2
\&Server Procedures
.IX "NFS server procedures" "" "" "" PAGE MAJOR
.LP
The  protocol definition  is given as   a  set  of  procedures with
arguments  and results defined using the   RPC  language.   A brief
description of the function of each procedure should provide enough
information to allow implementation.
.LP
All of  the procedures  in   the NFS  protocol  are assumed  to  be
synchronous.   When a procedure  returns to the  client, the client
can assume that the operation has completed and any data associated
with the request is  now on stable storage.  For  example, a client
.I WRITE 
request   may  cause  the   server  to  update  data  blocks,
filesystem information blocks (such as indirect  blocks),  and file
attribute  information (size  and  modify  times).  When  the 
.I WRITE 
returns to the client, it can assume  that the write  is safe, even
in case of  a server  crash, and  it can discard the  data written.
This is a very important part  of the statelessness  of the server.
If the server waited to flush data from remote requests, the client
would have to  save those requests so that  it could resend them in
case of a server crash.
.ie t .DS
.el .DS L

.ft I
/*
* Remote file service routines
*/
.ft CW
program NFS_PROGRAM {
	version NFS_VERSION {
		void        NFSPROC_NULL(void)              = 0;
		attrstat    NFSPROC_GETATTR(fhandle)        = 1;
		attrstat    NFSPROC_SETATTR(sattrargs)      = 2;
		void        NFSPROC_ROOT(void)              = 3;
		diropres    NFSPROC_LOOKUP(diropargs)       = 4;
		readlinkres NFSPROC_READLINK(fhandle)       = 5;
		readres     NFSPROC_READ(readargs)          = 6;
		void        NFSPROC_WRITECACHE(void)        = 7;
		attrstat    NFSPROC_WRITE(writeargs)        = 8;
		diropres    NFSPROC_CREATE(createargs)      = 9;
		stat        NFSPROC_REMOVE(diropargs)       = 10;
		stat        NFSPROC_RENAME(renameargs)      = 11;
		stat        NFSPROC_LINK(linkargs)          = 12;
		stat        NFSPROC_SYMLINK(symlinkargs)    = 13;
		diropres    NFSPROC_MKDIR(createargs)       = 14;
		stat        NFSPROC_RMDIR(diropargs)        = 15;
		readdirres  NFSPROC_READDIR(readdirargs)    = 16;
		statfsres   NFSPROC_STATFS(fhandle)         = 17;
	} = 2;
} = 100003;
.DE
.KS
.NH 3
\&Do Nothing
.IX "NFS server procedures" NFSPROC_NULL() "" \fINFSPROC_NULL()\fP
.DS
void 
NFSPROC_NULL(void) = 0;
.DE
.KE
This procedure does no work.   It is made available  in  all RPC
services to allow server response testing and timing.
.KS
.NH 3
\&Get File Attributes
.IX "NFS server procedures" NFSPROC_GETATTR() "" \fINFSPROC_GETATTR()\fP
.DS
attrstat 
NFSPROC_GETATTR (fhandle) = 1;
.DE
.KE
If the reply  status is 
.I NFS_OK ,
then  the reply attributes contains
the attributes for the file given by the input fhandle.
.KS
.NH 3
\&Set File Attributes
.IX "NFS server procedures" NFSPROC_SETATTR() "" \fINFSPROC_SETATTR()\fP
.DS
struct sattrargs {
	fhandle file;
	sattr attributes;
	};

attrstat
NFSPROC_SETATTR (sattrargs) = 2;
.DE
.KE
The  "attributes" argument  contains fields which are either  -1 or
are  the  new value for  the  attributes of  "file".   If the reply
status is 
.I NFS_OK ,
then the  reply attributes have the attributes of
the file after the "SETATTR" operation has completed.
.LP
Note: The use of -1 to indicate an unused field in "attributes" is
changed in the next version of the protocol.
.KS
.NH 3
\&Get Filesystem Root
.IX "NFS server procedures" NFSPROC_ROOT "" \fINFSPROC_ROOT\fP
.DS
void 
NFSPROC_ROOT(void) = 3;
.DE
.KE
Obsolete.  This procedure  is no longer used   because  finding the
root file handle of a filesystem requires moving  pathnames between
client  and server.  To  do  this right we would  have  to define a
network standard representation of pathnames.  Instead, the
function  of  looking up  the   root  file handle  is  done  by the
.I MNTPROC_MNT() 
procedure.    (See the
.I "Mount Protocol Definition"
later in this chapter for details).
.KS
.NH 3
\&Look Up File Name
.IX "NFS server procedures" NFSPROC_LOOKUP() "" \fINFSPROC_LOOKUP()\fP
.DS
diropres
NFSPROC_LOOKUP(diropargs) = 4;
.DE
.KE
If  the reply "status"  is 
.I NFS_OK ,
then the reply  "file" and reply
"attributes" are the file handle and attributes for the file "name"
in the directory given by "dir" in the argument.
.KS
.NH 3
\&Read From Symbolic Link
.IX "NFS server procedures" NFSPROC_READLINK() "" \fINFSPROC_READLINK()\fP
.DS
union readlinkres switch (stat status) {
	case NFS_OK:
		path data;
	default:
		void;
};

readlinkres
NFSPROC_READLINK(fhandle) = 5;
.DE
.KE
If "status" has the value 
.I NFS_OK ,
then the reply "data" is the data in 
the symbolic link given by the file referred to by the fhandle argument.
.LP
Note:  since   NFS always  parses pathnames    on the  client, the
pathname in  a symbolic  link may  mean something  different (or be
meaningless) on a different client or on the server if  a different
pathname syntax is used.
.KS
.NH 3
\&Read From File
.IX "NFS server procedures" NFSPROC_READ "" \fINFSPROC_READ\fP
.DS
struct readargs {
	fhandle file;
	unsigned offset;
	unsigned count;
	unsigned totalcount;
};

union readres switch (stat status) {
	case NFS_OK:
		fattr attributes;
		opaque data<NFS_MAXDATA>;
	default:
		void;
};

readres
NFSPROC_READ(readargs) = 6;
.DE
.KE
Returns  up  to  "count" bytes of   "data" from  the file  given by
"file", starting at "offset" bytes from  the beginning of the file.
The first byte of the file is  at offset zero.  The file attributes
after the read takes place are returned in "attributes".
.LP
Note: The  argument "totalcount" is  unused, and is removed in the
next protocol revision.
.KS
.NH 3
\&Write to Cache
.IX "NFS server procedures" NFSPROC_WRITECACHE() "" \fINFSPROC_WRITECACHE()\fP
.DS
void
NFSPROC_WRITECACHE(void) = 7;
.DE
.KE
To be used in the next protocol revision.
.KS
.NH 3
\&Write to File
.IX "NFS server procedures" NFSPROC_WRITE() "" \fINFSPROC_WRITE()\fP
.DS
struct writeargs {
	fhandle file;          
	unsigned beginoffset;  
	unsigned offset;       
	unsigned totalcount;   
	opaque data<NFS_MAXDATA>;
};

attrstat	
NFSPROC_WRITE(writeargs) = 8;
.DE
.KE
Writes   "data" beginning  "offset"  bytes  from the  beginning  of
"file".  The first byte  of  the file is at  offset  zero.  If  the
reply "status" is NFS_OK, then  the reply "attributes" contains the
attributes  of the file after the  write has  completed.  The write
operation is atomic.  Data from this  call to 
.I WRITE 
will not be mixed with data from another client's calls.
.LP
Note: The arguments "beginoffset" and "totalcount" are ignored and
are removed in the next protocol revision.
.KS
.NH 3
\&Create File
.IX "NFS server procedures" NFSPROC_CREATE() "" \fINFSPROC_CREATE()\fP
.DS
struct createargs {
	diropargs where;
	sattr attributes;
};

diropres
NFSPROC_CREATE(createargs) = 9;
.DE
.KE
The file "name" is created  in the directory given  by "dir".   The
initial  attributes of the  new file  are given by "attributes".  A
reply "status"  of NFS_OK indicates that the  file was created, and
reply "file"   and   reply "attributes"  are    its file handle and
attributes.   Any  other reply  "status"  means that  the operation
failed and no file was created.
.LP
Note: This  routine should pass  an exclusive create flag, meaning
"create the file only if it is not already there".
.KS
.NH 3
\&Remove File
.IX "NFS server procedures" NFSPROC_REMOVE() "" \fINFSPROC_REMOVE()\fP
.DS
stat
NFSPROC_REMOVE(diropargs) = 10;
.DE
.KE
The file "name" is  removed from the directory  given by "dir".   A
reply of NFS_OK means the directory entry was removed.
.LP
Note: possibly non-idempotent operation.
.KS
.NH 3
\&Rename File
.IX "NFS server procedures" NFSPROC_RENAME() "" \fINFSPROC_RENAME()\fP
.DS
struct renameargs {
	diropargs from;	
	diropargs to;
};

stat
NFSPROC_RENAME(renameargs) = 11;
.DE
.KE
The existing file "from.name" in  the directory given by "from.dir"
is renamed to "to.name" in the directory given by "to.dir".  If the
reply  is 
.I NFS_OK ,
the file was  renamed.  The  
RENAME  
operation is
atomic on the server; it cannot be interrupted in the middle.
.LP
Note: possibly non-idempotent operation.
.KS
.NH 3
\&Create Link to File
.IX "NFS server procedures" NFSPROC_LINK() "" \fINFSPROC_LINK()\fP
.DS
struct linkargs {
	fhandle from;
	diropargs to;
};

stat
NFSPROC_LINK(linkargs) = 12;
.DE
.KE
Creates the  file "to.name"  in the directory  given   by "to.dir",
which is a hard link to the existing file given  by "from".  If the
return value is 
.I NFS_OK ,
a link was created.  Any other return value
indicates an error, and the link was not created.
.LP
A hard link should have the property that changes  to either of the
linked files are reflected in both files.  When a hard link is made
to a  file, the attributes  for  the file should  have  a value for
"nlink" that is one greater than the value before the link.
.LP
Note: possibly non-idempotent operation.
.KS
.NH 3
\&Create Symbolic Link
.IX "NFS server procedures" NFSPROC_SYMLINK() "" \fINFSPROC_SYMLINK()\fP
.DS
struct symlinkargs {
	diropargs from;
	path to;
	sattr attributes;
};

stat
NFSPROC_SYMLINK(symlinkargs) = 13;
.DE
.KE
Creates the  file "from.name" with  ftype  
.I NFLNK 
in  the  directory
given by "from.dir".   The new file contains  the pathname "to" and
has initial attributes given by "attributes".  If  the return value
is 
.I NFS_OK ,
a link was created.  Any other return value indicates an
error, and the link was not created.
.LP
A symbolic  link is  a pointer to another file.   The name given in
"to" is  not interpreted by  the server, only stored in  the  newly
created file.  When the client references a file that is a symbolic
link, the contents of the symbolic  link are normally transparently
reinterpreted  as a pathname  to substitute.   A 
.I READLINK 
operation returns the data to the client for interpretation.
.LP
Note:  On UNIX servers the attributes are never used, since
symbolic links always have mode 0777.
.KS
.NH 3
\&Create Directory
.IX "NFS server procedures" NFSPROC_MKDIR() "" \fINFSPROC_MKDIR()\fP
.DS
diropres
NFSPROC_MKDIR (createargs) = 14;
.DE
.KE
The new directory "where.name" is created in the directory given by
"where.dir".  The initial attributes of the new directory are given
by "attributes".  A reply "status" of NFS_OK indicates that the new
directory was created, and reply "file" and  reply "attributes" are
its file  handle and attributes.  Any  other  reply "status"  means
that the operation failed and no directory was created.
.LP
Note: possibly non-idempotent operation.
.KS
.NH 3
\&Remove Directory
.IX "NFS server procedures" NFSPROC_RMDIR() "" \fINFSPROC_RMDIR()\fP
.DS
stat
NFSPROC_RMDIR(diropargs) = 15;
.DE
.KE
The existing empty directory "name" in the directory given by "dir"
is removed.  If the reply is 
.I NFS_OK ,
the directory was removed.
.LP
Note: possibly non-idempotent operation.
.KS
.NH 3
\&Read From Directory
.IX "NFS server procedures" NFSPROC_READDIR() "" \fINFSPROC_READDIR()\fP
.DS
struct readdirargs {
	fhandle dir;            
	nfscookie cookie;
	unsigned count;         
};

struct entry {
	unsigned fileid;
	filename name;
	nfscookie cookie;
	entry *nextentry;
};

union readdirres switch (stat status) {
	case NFS_OK:
		struct {
			entry *entries;
			bool eof;
		} readdirok;
	default:
		void;
};

readdirres
NFSPROC_READDIR (readdirargs) = 16;
.DE
.KE
Returns a variable number of  directory entries,  with a total size
of up to "count" bytes, from the directory given  by "dir".  If the
returned  value of "status"  is 
.I NFS_OK ,
then  it  is followed  by a
variable  number  of "entry"s.    Each "entry" contains  a "fileid"
which consists of a  unique number  to identify the  file within  a
filesystem,  the  "name" of the  file, and a "cookie" which   is an
opaque pointer to the next entry in  the  directory.  The cookie is
used  in the next  
.I READDIR 
call to get more  entries  starting at a
given point in  the directory.  The  special cookie zero (all  bits
zero) can be used to get the entries starting  at the  beginning of
the directory.  The "fileid" field should be the same number as the
"fileid" in the attributes of the file.  (See the
.I "Basic Data Types"
section.) 
The "eof" flag has a value of
.I TRUE 
if there are no more entries in the directory.
.KS
.NH 3
\&Get Filesystem Attributes
.IX "NFS server procedures" NFSPROC_STATFS() "" \fINFSPROC_STATFS()\fP
.DS
union statfsres (stat status) {
	case NFS_OK:
		struct {
			unsigned tsize; 
			unsigned bsize; 
			unsigned blocks;
			unsigned bfree; 
			unsigned bavail;
		} info;
	default:
		void;
};

statfsres
NFSPROC_STATFS(fhandle) = 17;
.DE
.KE
If the  reply "status"  is 
.I NFS_OK ,
then the  reply "info" gives the
attributes for the filesystem that contains file referred to by the
input fhandle.  The attribute fields contain the following values:
.IP tsize:   
The optimum transfer size of the server in bytes.  This is
the number  of bytes the server  would like to have in the
data part of READ and WRITE requests.
.IP bsize:   
The block size in bytes of the filesystem.
.IP blocks:  
The total number of "bsize" blocks on the filesystem.
.IP bfree:   
The number of free "bsize" blocks on the filesystem.
.IP bavail:  
The number of  "bsize" blocks  available to non-privileged users.
.LP
Note: This call does not  work well if a  filesystem has  variable
size blocks.
.NH 1
\&NFS Implementation Issues
.IX NFS implementation
.LP
The NFS protocol is designed to be operating system independent, but
since this version was designed in a UNIX environment, many
operations have semantics similar to the operations of the UNIX file
system.  This section discusses some of the implementation-specific
semantic issues.
.NH 2
\&Server/Client Relationship
.IX NFS "server/client relationship"
.LP
The NFS protocol is designed to allow servers to be as simple and
general as possible.  Sometimes the simplicity of the server can be a
problem, if the client wants to implement complicated filesystem
semantics.
.LP
For example, some operating systems allow removal of open files.  A
process can open a file and, while it is open, remove it from the
directory.  The file can be read and written as long as the process
keeps it open, even though the file has no name in the filesystem.
It is impossible for a stateless server to implement these semantics.
The client can do some tricks such as renaming the file on remove,
and only removing it on close.  We believe that the server provides
enough functionality to implement most file system semantics on the
client.
.LP
Every NFS client can also potentially be a server, and remote and
local mounted filesystems can be freely intermixed.  This leads to
some interesting problems when a client travels down the directory
tree of a remote filesystem and reaches the mount point on the server
for another remote filesystem.  Allowing the server to follow the
second remote mount would require loop detection, server lookup, and
user revalidation.  Instead, we decided not to let clients cross a
server's mount point.  When a client does a LOOKUP on a directory on
which the server has mounted a filesystem, the client sees the
underlying directory instead of the mounted directory.  A client can
do remote mounts that match the server's mount points to maintain the
server's view.
.LP
.NH 2
\&Pathname Interpretation
.IX NFS "pathname interpretation"
.LP
There are a few complications to the rule that pathnames are always
parsed on the client.  For example, symbolic links could have
different interpretations on different clients.  Another common
problem for non-UNIX implementations is the special interpretation of
the pathname ".."  to mean the parent of a given directory.  The next
revision of the protocol uses an explicit flag to indicate the parent
instead.
.NH 2
\&Permission Issues
.IX NFS "permission issues"
.LP
The NFS protocol, strictly speaking, does not define the permission
checking used  by servers.  However,  it is  expected that a server
will do normal operating system permission checking using 
.I AUTH_UNIX 
style authentication as the basis of its protection mechanism.  The
server gets the client's effective "uid", effective "gid", and groups
on each call and uses them to check permission.  There are various
problems with this method that can been resolved in interesting ways.
.LP
Using "uid" and "gid" implies that the client and server share the
same "uid" list.  Every server and client pair must have the same
mapping from user to "uid" and from group to "gid".  Since every
client can also be a server, this tends to imply that the whole
network shares the same "uid/gid" space.
.I AUTH_DES 
(and the  next
revision of the NFS protocol) uses string names instead of numbers,
but there are still complex problems to be solved.
.LP
Another problem arises due to the usually stateful open operation.
Most operating systems check permission at open time, and then check
that the file is open on each read and write request.  With stateless
servers, the server has no idea that the file is open and must do
permission checking on each read and write call.  On a local
filesystem, a user can open a file and then change the permissions so
that no one is allowed to touch it, but will still be able to write
to the file because it is open.  On a remote filesystem, by contrast,
the write would fail.  To get around this problem, the server's
permission checking algorithm should allow the owner of a file to
access it regardless of the permission setting.
.LP
A similar problem has to do with paging in from a file over the
network.  The operating system usually checks for execute permission
before opening a file for demand paging, and then reads blocks from
the open file.  The file may not have read permission, but after it
is opened it doesn't matter.  An NFS server can not tell the
difference between a normal file read and a demand page-in read.  To
make this work, the server allows reading of files if the "uid" given
in the call has execute or read permission on the file.
.LP
In most operating systems, a particular user (on the user ID zero)
has access to all files no matter what permission and ownership they
have.  This "super-user" permission may not be allowed on the server,
since anyone who can become super-user on their workstation could
gain access to all remote files.  The UNIX server by default maps
user id 0 to -2 before doing its access checking.  This works except
for NFS root filesystems, where super-user access cannot be avoided.
.NH 2
\&Setting RPC Parameters
.IX NFS "setting RPC parameters"
.LP
Various file system parameters and options should be set at mount
time.  The mount protocol is described in the appendix below.  For
example, "Soft" mounts as well as "Hard" mounts are usually both
provided.  Soft mounted file systems return errors when RPC
operations fail (after a given number of optional retransmissions),
while hard mounted file systems continue to retransmit forever.
Clients and servers may need to keep caches of recent operations to
help avoid problems with non-idempotent operations.
.NH 1
\&Mount Protocol Definition
.IX "mount protocol" "" "" "" PAGE MAJOR
.sp 1
.NH 2
\&Introduction
.IX "mount protocol" introduction
.LP
The mount protocol is separate from, but related to, the NFS
protocol.  It provides operating system specific services to get the
NFS off the ground -- looking up server path names, validating user
identity, and checking access permissions.  Clients use the mount
protocol to get the first file handle, which allows them entry into a
remote filesystem.
.LP
The mount protocol is kept separate from the NFS protocol to make it
easy to plug in new access checking and validation methods without
changing the NFS server protocol.
.LP
Notice that the protocol definition implies stateful servers because
the server maintains a list of client's mount requests.  The mount
list information is not critical for the correct functioning of
either the client or the server.  It is intended for advisory use
only, for example, to warn possible clients when a server is going
down.
.LP
Version one of the mount protocol is used with version two of the NFS
protocol.  The only connecting point is the
.I fhandle 
structure, which is the same for both protocols.
.NH 2
\&RPC Information
.IX "mount protocol"  "RPC information"
.IP \fIAuthentication\fP
The mount service uses 
.I AUTH_UNIX 
and 
.I AUTH_DES 
style authentication only.
.IP "\fITransport Protocols\fP"
The mount service is currently supported on UDP/IP only.
.IP "\fIPort Number\fP"
Consult the server's portmapper, described in the chapter
.I "Remote Procedure Calls: Protocol Specification",
to  find  the  port number on which the mount service is registered.
.NH 2
\&Sizes of XDR Structures
.IX "mount protocol" "XDR structure sizes"
.LP
These  are  the sizes,   given  in  decimal   bytes, of various XDR
structures used in the protocol:
.DS
/* \fIThe maximum number of bytes in a pathname argument\fP */
const MNTPATHLEN = 1024;

/* \fIThe maximum number of bytes in a name argument\fP */
const MNTNAMLEN = 255;

/* \fIThe size in bytes of the opaque file handle\fP */
const FHSIZE = 32;
.DE
.NH 2
\&Basic Data Types
.IX "mount protocol" "basic data types"
.IX "mount data types"
.LP
This section presents the data  types used by  the  mount protocol.
In many cases they are similar to the types used in NFS.
.KS
.NH 3
\&fhandle
.IX "mount data types" fhandle "" \fIfhandle\fP
.DS
typedef opaque fhandle[FHSIZE];
.DE
.KE
The type 
.I fhandle 
is the file handle that the server passes to the
client.  All file operations are done  using file handles  to refer
to a  file  or directory.   The  file handle  can  contain whatever
information the server needs to distinguish an individual file.
.LP
This  is the  same as the "fhandle" XDR definition in version 2 of
the NFS protocol;  see 
.I "Basic Data Types"
in the definition of the NFS protocol, above.
.KS
.NH 3
\&fhstatus
.IX "mount data types" fhstatus "" \fIfhstatus\fP
.DS
union fhstatus switch (unsigned status) {
	case 0:
		fhandle directory;
	default:
		void;
};
.DE
.KE
The type 
.I fhstatus 
is a union.  If a "status" of zero is returned,
the  call completed   successfully, and  a  file handle   for   the
"directory"  follows.  A  non-zero  status indicates  some  sort of
error.  In this case the status is a UNIX error number.
.KS
.NH 3
\&dirpath
.IX "mount data types" dirpath "" \fIdirpath\fP
.DS
typedef string dirpath<MNTPATHLEN>;
.DE
.KE
The type 
.I dirpath 
is a server pathname of a directory.
.KS
.NH 3
\&name
.IX "mount data types" name "" \fIname\fP
.DS
typedef string name<MNTNAMLEN>;
.DE
.KE
The type 
.I name 
is an arbitrary string used for various names.
.NH 2
\&Server Procedures
.IX "mount server procedures"
.LP
The following sections define the RPC procedures  supplied by a
mount server.
.ie t .DS
.el .DS L
.ft I
/*
* Protocol description for the mount program
*/
.ft CW

program MOUNTPROG {
.ft I
/*
* Version 1 of the mount protocol used with
* version 2 of the NFS protocol.
*/
.ft CW
	version MOUNTVERS {
		void        MOUNTPROC_NULL(void)    = 0;
		fhstatus    MOUNTPROC_MNT(dirpath)  = 1;
		mountlist   MOUNTPROC_DUMP(void)    = 2;
		void        MOUNTPROC_UMNT(dirpath) = 3;
		void        MOUNTPROC_UMNTALL(void) = 4;
		exportlist  MOUNTPROC_EXPORT(void)  = 5;
	} = 1;
} = 100005;
.DE
.KS
.NH 3
\&Do Nothing
.IX "mount server procedures" MNTPROC_NULL() "" \fIMNTPROC_NULL()\fP
.DS
void 
MNTPROC_NULL(void) = 0;
.DE
.KE
This  procedure does no work.  It   is  made  available in all  RPC
services to allow server response testing and timing.
.KS
.NH 3
\&Add Mount Entry
.IX "mount server procedures" MNTPROC_MNT() "" \fIMNTPROC_MNT()\fP
.DS
fhstatus
MNTPROC_MNT(dirpath) = 1;
.DE
.KE
If the reply "status" is 0, then the reply "directory" contains the
file handle for the directory "dirname".  This file handle may be
used in the NFS protocol.  This procedure also adds a new entry to
the mount list for this client mounting "dirname".
.KS
.NH 3
\&Return Mount Entries
.IX "mount server procedures" MNTPROC_DUMP() "" \fIMNTPROC_DUMP()\fP
.DS
struct *mountlist {
	name      hostname;
	dirpath   directory;
	mountlist nextentry;
};

mountlist
MNTPROC_DUMP(void) = 2;
.DE
.KE
Returns  the list of  remote mounted filesystems.   The "mountlist"
contains one entry for each "hostname" and "directory" pair.
.KS
.NH 3
\&Remove Mount Entry
.IX "mount server procedures" MNTPROC_UMNT() "" \fIMNTPROC_UMNT()\fP
.DS
void
MNTPROC_UMNT(dirpath) = 3;
.DE
.KE
Removes the mount list entry for the input "dirpath".
.KS
.NH 3
\&Remove All Mount Entries
.IX "mount server procedures" MNTPROC_UMNTALL() "" \fIMNTPROC_UMNTALL()\fP
.DS
void
MNTPROC_UMNTALL(void) = 4;
.DE
.KE
Removes all of the mount list entries for this client.
.KS
.NH 3
\&Return Export List
.IX "mount server procedures" MNTPROC_EXPORT() "" \fIMNTPROC_EXPORT()\fP
.DS
struct *groups {
	name grname;
	groups grnext;
};

struct *exportlist {
	dirpath filesys;
	groups groups;
	exportlist next;
};

exportlist
MNTPROC_EXPORT(void) = 5;
.DE
.KE
Returns a variable number of export list entries.  Each entry
contains a filesystem name and a list of groups that are allowed to
import it.  The filesystem name is in "filesys", and the group name
is in the list "groups".
.LP
Note:  The exportlist should contain
more information about the status of the filesystem, such as a
read-only flag.
