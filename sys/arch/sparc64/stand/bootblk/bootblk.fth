\	$OpenBSD: bootblk.fth,v 1.11 2024/09/04 07:54:52 mglocker Exp $
\	$NetBSD: bootblk.fth,v 1.15 2015/08/20 05:40:08 dholland Exp $
\
\	IEEE 1275 Open Firmware Boot Block
\
\	Parses disklabel and UFS and loads the file called `ofwboot'
\
\
\	Copyright (c) 1998-2010 Eduardo Horvath.
\	All rights reserved.
\
\	Redistribution and use in source and binary forms, with or without
\	modification, are permitted provided that the following conditions
\	are met:
\	1. Redistributions of source code must retain the above copyright
\	   notice, this list of conditions and the following disclaimer.
\	2. Redistributions in binary form must reproduce the above copyright
\	   notice, this list of conditions and the following disclaimer in the
\	   documentation and/or other materials provided with the distribution.
\
\	THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
\	IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
\	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
\	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
\	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
\	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
\	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
\	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
\	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
\	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\

offset16
hex
headers

false value boot-debug?

: KB d# 1024 * ;

\
\ First some housekeeping:  Open /chosen and set up vectors into
\	client-services

" /chosen" find-package 0=  if ." Cannot find /chosen" 0 then
constant chosen-phandle

" /openprom/client-services" find-package 0=  if 
	." Cannot find client-services" cr abort
then constant cif-phandle

defer cif-claim ( align size virt -- base )
defer cif-release ( size virt -- )
defer cif-open ( cstr -- ihandle|0 )
defer cif-close ( ihandle -- )
defer cif-read ( len adr ihandle -- #read )
defer cif-seek ( low high ihandle -- -1|0|1 )
\ defer cif-peer ( phandle -- phandle )
\ defer cif-getprop ( len adr cstr phandle -- )

: find-cif-method ( method len -- xf )
   cif-phandle find-method drop 
;

" claim" find-cif-method  to  cif-claim
" open" find-cif-method  to  cif-open
" close" find-cif-method  to  cif-close
" read" find-cif-method  to  cif-read
" seek" find-cif-method  to  cif-seek

: twiddle ( -- ) ." ." ; \ Need to do this right.  Just spit out periods for now.

\
\ Support routines
\

\ 64-bit math support

here h# ffff over l! <w@ constant little-endian?
: ul>d ( l -- d.lo d.hi )	0 ;
: l>d ( l -- d.lo d.hi )	dup 0<  if  -1  else  0  then ;
: d>l ( d.lo d.hi -- l )	drop ;
: d@ ( addr -- d.lo d.hi )	dup l@ swap la1+ l@ little-endian? invert  if  swap  then ;
: d! ( d.lo d.hi addr -- )
   little-endian? invert  if  -rot swap rot  then  tuck la1+ l! l! ;
: d-and ( d1 d2 -- d1-and-d2 )  rot and -rot and swap ;
: d*u ( d1 u -- d2 )		tuck um* drop -rot um* rot + ;
: d<< ( d1 n -- d1<<n )	\ Hope this works
   ?dup  if  \ Shifting by 0 doesn't appear to work properly.
      tuck <<			( d.lo n d.hi' )
      -rot 2dup <<		( d.hi' d.lo n d.lo' )
      -rot d# 32 swap - >>	( d.hi' d.lo' lo.hi )
      rot +
   then
;
: d>> ( d1 n -- d1>>n )	\ Hope this works
   ?dup  if  \ Shifting by 0 doesn't appear to work properly.
      rot over >>	-rot	( d.lo' d.hi n )
      2dup >> -rot		( d.lo' d.hi' d.hi n )
      d# 32 swap - << rot + swap
   then
;
: d> ( d1 d2 -- d1>d2? )
   rot swap 2dup = if
      2drop > exit
   then
   > nip nip
;
: d>= ( d1 d2 -- d1>=d2? )
   rot swap 2dup =  if
      2drop >= exit
   then
   >= nip nip
;
: d< ( d1 d2 -- d1<d2? )	d>= invert ;
: d= ( d1 d2 -- d1=d2? )	rot = -rot = and ;
: d<> ( d1 d2 -- d1<>d2? )	d= invert ;


\ String support 

: strcmp ( s1 l1 s2 l2 -- true:false )
   rot tuck <>  if  3drop false exit  then
   comp 0=
;

\ Move string into buffer

: strmov ( s1 l1 d -- d l1 )
   dup 2over swap -rot		( s1 l1 d s1 d l1 )
   move				( s1 l1 d )
   rot drop swap
;

\ Move s1 on the end of s2 and return the result

: strcat ( s1 l1 s2 l2 -- d tot )
   2over swap 				( s1 l1 s2 l2 l1 s1 )
   2over + rot				( s1 l1 s2 l2 s1 d l1 )
   move rot + 				( s1 s2 len )
   rot drop				( s2 len )
;

: strchr ( s1 l1 c -- s2 l2 )
   begin
      dup 2over 0= if			( s1 l1 c c s1  )
         2drop drop exit then
      c@ = if				( s1 l1 c )
         drop exit then
      -rot /c - swap ca1+		( c l2 s2 )
     swap rot
  again
;

   
: cstr ( ptr -- str len )
   dup 
   begin dup c@ 0<>  while + repeat
   over -
;

\
\ BSD UFS parameters
\

fload	ffs.fth.h

sbsize buffer: sb-buf
-1 value boot-ihandle
dev_bsize value bsize

: strategy ( addr size db.lo db.hi -- nread )
    bsize d*u				( addr size sector.lo sector.hi )
    " seek" boot-ihandle $call-method -1 = if 
	." strategy: Seek failed" cr
	abort
    then				( addr size )
    " read" boot-ihandle $call-method
;


\
\ Multi-FS support
\
\ XXX Maybe the different filesystems should be segregated into separate files
\ XXX that are individually loaded.
\

defer fs-size
defer di-size
defer di-mode
defer /dino
defer cgstart
defer di-db@
defer di-ib@
defer ib-ib@
defer fs-bsize
defer fsbtodb
defer blksize
defer lblkno
defer blkoff
defer read-inode
\ LFS ifile
defer /ifile
defer if_daddr

\
\ FFS Cylinder group macros
\

: cgdmin ( cg fs -- d-1st-data-block )	dup fs_dblkno l@ l>d 2swap cgstart d+ ;
: cgimin ( cg fs -- d-inode-block )	dup fs_iblkno l@ l>d 2swap cgstart d+ ;
: cgsblock ( cg fs -- d-super-block )	dup fs_sblkno l@ l>d 2swap cgstart d+ ;
: cgstod ( cg fs -- d-cg-block )	dup fs_cblkno l@ l>d 2swap cgstart d+ ;

\
\ FFS Block and frag position macros
\

: ffs-blkoff ( pos.lo pos.hi fs -- off.lo off.hi )	fs_qbmask d@ d-and ;
\ : ffs-fragoff ( pos.lo pos.hi fs -- off.lo off.hi )	fs_qfmask d@ d-and ;
\ : ffs-lblktosize ( blk fs -- off.lo off.hi )		0 fs_bshift l@ d<< ;
: ffs-lblkno ( pos.lo pos.hi fs -- off.lo off.hi )	fs_bshift l@ d>> ;
: ffs-numfrags ( pos.lo pos.hi fs -- off.lo off.hi )	fs_fshift l@ d>> ;
: ffs-blkroundup ( pos.lo pos.hi fs -- off.lo off.hi )
    >r r@ fs_qbmask d@ d+ r> fs_bmask l@ l>d d-and
;
: ffs-fragroundup ( pos.lo pos.hi fs -- off.lo off.hi )
    >r r@ fs_qfmask d@ d+ r> fs_fmask l@ l>d d-and
;
: ffs-fragstoblks ( pos.lo pos.hi fs -- off.lo off.hi )	fs_fragshift l@ d>> ;
: ffs-blkstofrags ( blk fs -- frag )			fs_fragshift l@ << ;
\ : ffs-fragnum ( fsb fs -- off )			fs_frag l@ 1- and ;
\ : ffs-blknum ( fsb fs -- off )			fs_frag l@ 1- not and ;
: ffs-dblksize ( lbn.lo lbn.hi inodep fs -- size )
   >r -rot 2dup ndaddr l>d d>		( inop d-lbn >ndaddr? )
   -rot 1 0 d+				( inop >ndaddr? d-lbn+1 )
   r@ fs_bshift l@ d<<			( inop >ndaddr? d-lbn+1<<bshift )
   2swap >r di-size d@			( d-lbn+1<<bshift d-size )
   2swap 2over d< r> or  if		( d-size )
	2drop r> fs-bsize l@ exit
    then
    r@ ffs-blkoff			( size.lo size.hi )
    r> ffs-fragroundup d>l		( size )
;

: ino-to-cg ( ino fs -- cg )		fs_ipg l@ / ;
: ino-to-fsbo ( ino fs -- fsb0 )	fs_inopb l@ mod ;
: ino-to-fsba ( ino fs -- ba.lo ba.hi )	\ Need to remove the stupid stack diags someday
   2dup 				( ino fs ino fs )
   ino-to-cg				( ino fs cg )
   over					( ino fs cg fs )
   cgimin				( ino fs inode-blk.lo inode-blk.hi )
   2swap				( d-inode-blk ino fs )
   tuck 				( d-inode-blk fs ino fs )
   fs_ipg l@ 				( d-inode-blk fs ino ipg )
   mod					( d-inode-blk fs mod )
   swap					( d-inode-blk mod fs )
   dup 					( d-inode-blk mod fs fs )
   fs_inopb l@ 				( d-inode-blk mod fs inopb )
   rot 					( d-inode-blk fs inopb mod )
   swap					( d-inode-blk fs mod inopb )
   /					( d-inode-blk fs div )
   swap					( d-inode-blk div fs )
   ffs-blkstofrags			( d-inode-blk frag )
   0 d+
;
: ffs-fsbtodb ( fsb.lo fsb.hi fs -- db.lo db.hi )
    fs_fsbtodb l@ d<<
;

\ \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\
\
\ The rest of the multi-filesystem stuff
\
\ \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\

\
\ FFS v1
\
: di-db-v1@ ( indx dinode -- db.lo db.hi )	di1_db swap la+ l@ l>d ;
: di-ib-v1@ ( indx dinode -- db.lo db.hi )	di1_ib swap la+ l@ l>d ;
: ib-ib-v1@ ( indx iblk -- db.lo db.hi )	swap la+ l@ l>d ;

: cgbase ( cg fs -- daddr.lo daddr.hi ) fs_fpg l@ um* ;
: cgstart-ufs1 ( cg fs -- cgstart ) 
    2dup fs_cgmask l@ invert and		( cg fs stuff )
    over fs_cgoffset l@ um*			( cg fs off.lo off.hi )
    2swap cgbase d+				( off.lo off.hi )
;

\
\ FFS v2
\

: di-db-v2@ ( indx dinode -- db.lo db.hi )	di2_db swap 2* la+ d@ ;
: di-ib-v2@ ( indx dinode -- db.lo db.hi )	di2_ib swap 2* la+ d@ ;
: ib-ib-v2@ ( indx iblk -- db.lo db.hi )	2* la+ d@ ;

\
\ LFS v1
\


\
\ File stuff
\

niaddr /w* constant narraysize

\ Assume UFS2 dinodes are always bigger than UFS1
ufs2_dinode_SIZEOF buffer: cur-inode
0 value indir-block
create indir-addr -1 , -1 ,

\
\ Translate a fileblock to a disk block
\
\ We don't do triple indirect blocks.
\

\ Get the disk address from a single indirect block
: ib@ ( indx indir.lo indir.hi -- db.lo db.hi )
    2dup indir-addr d@ d<>  if		( indx indir.hi indir.lo )
	indir-addr d!			( indx )
	indir-block 			( indx indir-block )
	sb-buf fs-bsize l@		( indx indir-block fs fs-bsize )
	indir-addr d@ sb-buf		( indx indir-block fs-bsize indiraddr fs )
	fsbtodb 			( indx indir-block fs-bsize db.lo db.hi )
	strategy 0			( indx nread 0 ) \ Really should check return value
    then
    2drop				( indx )
    indir-block ib-ib@
;


: block-map ( fileblock -- diskblock.lo diskblock.hi )
    \ Direct block?
    dup ndaddr <  if			( fileblock )
	cur-inode di-db@ exit		( diskblock.lo diskblock.hi )
    then 				( fileblock )
    ndaddr -				( fileblock' )
    \ Now we need to check the indirect block
    dup sb-buf fs_nindir l@ <  if	( fileblock' )
	0 cur-inode di-ib@		( fileblock' indir.lo indir.hi )
	ib@ exit			( db.lo db.hi )
   then
   dup sb-buf fs_nindir -		( fileblock'' )
   \ Now try 2nd level indirect block -- just read twice 
   dup sb-buf fs_nindir l@ dup * >= if	( fileblock'' )
       ." block-map: exceeded max file size" cr
       abort
   then
       
   1 cur-inode di-ib@		( fileblock'' ib.lo ib.hi )
   
   \ Get 1st indirect block and find the 2nd indirect block
   rot dup sb-buf fs_nindir u/mod	( ib2.lo ib2.hi indx2 indx1 )
   2swap ib@			( indx2 ib2.lo ib2.hi )
   
   \ Get 2nd indirect block and find our diskblock
   ib@				( db.lo db.hi )
;

\
\ Read file into internal buffer and return pointer and len
\

0 value cur-block			\ allocated dynamically in ufs-open
0 value cur-blocksize			\ size allocated  to  cur-block
create cur-blockno -1 l, -1 l,		\ Current disk block.
-1 value file-blockno			\ Current file block no.
0 value file-offset			\ Current file offset, max 4GB.

: buf-read-file ( fs -- buf len )
    >r file-offset			( seek )
    dup l>d r@ lblkno drop		( seek blk )
    dup l>d cur-inode r@ blksize	( seek blk blksize )
    over file-blockno <> if		( seek blk blksize )
	over  to  file-blockno
	swap block-map			( seek blksize fsblk.lo fsblk.hi )
	2dup or 0=  if			( seek blksize fsblk.lo fsblk.hi )
	    \ Clear out curblock  XXX Why? Idunno.
	    2drop dup
	    cur-block swap erase	( seek blksize )
	    boot-debug?  if ." buf-read-file reading block 0" cr then
	    -1 l>d			\ Invalid disk block
	else
	    \ Call strategy to load the correct block.
	    r@ fsbtodb			( seek blksize dblk.lo dblk.hi )
	    rot >r cur-block r@ 2over	( seek addr size db.lo db.hi )
	    strategy r@	<>  if  ." buf-read-file: short read." cr abort  then
	    r> -rot			( seek size db.lo db.hi )
	then
	\ Save the new current disk block number
	cur-blockno d!			( seek size )
   else					
      nip				( seek size )
   then
   \ Now figure out how much we have in the buffer.
   swap l>d r> blkoff			( size off.lo off.hi )
   d>l cur-block over +			( size off buf )
   -rot -				( buf siz )
;

\
\ Read inode into cur-inode -- uses cur-block
\

: read-inode-ffs ( inode fs -- )
    twiddle

    >r dup r@ ino-to-fsba		( ino fsblk.lo fsblck.hi )
    r@ fsbtodb				( ino dblk.lo dblk.hi )
    2dup cur-blockno d@ d<>  if		( ino dblk.lo dblk.hi )
	\ We need  to  read the block
	cur-block r@ fs-bsize l@	( ino dblk.lo dblk.hi addr size )
	>r r@ 2over strategy r> <> if	( ino dblk.lo dblk.hi )
	    ." read-inode - residual" cr abort
	then
	2dup cur-blockno d!		( ino dblk.lo dblk.hi )
    then 2drop				( ino )

    r> ino-to-fsbo /dino *		( off )
    cur-block + cur-inode /dino move	( )
;

\ Identify inode type

: is-dir? ( ufs1_dinode -- is-dir? )		di-mode w@ ifmt and ifdir = ;
: is-symlink? ( ufs1_dinode -- is-symlink? )	di-mode w@ ifmt and iflnk = ;

\
\ Multi-FS initialization.
\
\ It's way down here so all the fs-specific routines have already been defined.
\

: init-ffs-common ( -- )
   ' fs_SIZEOF  to  fs-size 
   ' fs_bsize  to  fs-bsize
   ' ffs-dblksize  to  blksize
   ' read-inode-ffs  to  read-inode
   ' ffs-fsbtodb  to  fsbtodb
   ' ffs-lblkno  to  lblkno
   ' ffs-blkoff  to   blkoff
;   


: ffs-oldcompat ( -- )
   \ Make sure old ffs values in sb-buf are sane
   sb-buf fs_npsect dup l@ sb-buf fs_nsect l@ max swap l!
   sb-buf fs_interleave dup l@ 1 max swap l!
   sb-buf fs_postblformat l@ fs_42postblfmt =  if
      8 sb-buf fs_nrpos l!
   then
   sb-buf fs_inodefmt l@ fs_44inodefmt <  if
      sb-buf fs-bsize l@ 
      dup ndaddr um* 1 d- sb-buf fs_maxfilesize d!
      niaddr 0  ?do
	 sb-buf fs_nindir l@ * dup	( sizebp sizebp )
	 sb-buf fs_maxfilesize dup d@ ( sizebp sizebp *mxfs mxfs.lo mxfs.hi )
	 2over drop l>d d+ 2swap d!	( sizebp )
      loop  drop 			( )
      sb-buf dup fs_bmask l@ invert l>d rot fs_qbmask d!
      sb-buf dup fs_fmask l@ invert l>d rot fs_qfmask d!
   then
;


: init-ffs-v1 ( -- )
   boot-debug?  if  ." FFS v1" cr  then
   init-ffs-common
   ' di1_size  to  di-size
   ' di1_mode  to  di-mode
   ' ufs1_dinode_SIZEOF  to  /dino
   ' cgstart-ufs1  to  cgstart
   ' di-db-v1@  to  di-db@
   ' di-ib-v1@  to  di-ib@
   ' ib-ib-v1@  to  ib-ib@
   ffs-oldcompat
;

: init-ffs-v2 ( -- )
   boot-debug?  if  ." FFS v2" cr  then
   init-ffs-common
   ' di2_size  to  di-size
   ' di2_mode  to  di-mode
   ' ufs2_dinode_SIZEOF  to  /dino
   ' cgbase  to  cgstart
   ' di-db-v2@  to  di-db@
   ' di-ib-v2@  to  di-ib@
   ' ib-ib-v2@  to  ib-ib@
;

: fs-magic? ( sb -- is-ufs? )
   \ The FFS magic is at the end of the superblock
   \ XXX we should check to make sure this is not an alternate SB.
   fs_magic l@  case
      fs1_magic_value  of  init-ffs-v1 true  endof
      fs2_magic_value  of  init-ffs-v2 true  endof
      false swap	\ Return false
   endcase
;



\
\ Hunt for directory entry:
\
\ repeat
\    load a buffer
\    while entries do
\       if entry == name return
\       next entry
\ until no buffers
\

: search-dir-block ( str len buf len -- ino | 0 )
    2dup + nip				( str len buf bufend )
    swap 2swap rot			( bufend str len direct )
    begin  dup 4 pick <  while		( bufend str len direct )
	    dup d_ino l@ 0<>  if	( bufend str len direct )
		boot-debug?  if
		    \ Print the current file name
		    dup dup d_name swap d_namlen c@ type cr
		then
		2dup d_namlen c@ =  if	( bufend str len direct )
		    dup d_name 2over	( bufend str len direct dname str len )
		    comp 0= if		( bufend str len direct )
			\ Found it -- return inode
			d_ino l@ nip nip nip	( dino )
			boot-debug?  if  ." Found it" cr  then 
			exit 		( dino )
		    then
		then			( bufend str len direct )
	    then			( bufend str len direct )
	    dup d_reclen w@ +		( bufend str len nextdirect )
    repeat
    2drop 2drop 0
;
    

: search-directory ( str len -- ino | 0 )
    0  to  file-offset
    begin
	file-offset cur-inode di-size d@ drop <
    while				( str len )
	    \ Read a directory block
	    sb-buf buf-read-file	( str len buf len )
	    dup 0=  if  ." search-directory: buf-read-file zero len" cr abort  then
	    dup file-offset +  to  file-offset	( str len buf len )

	    2over 2swap search-dir-block ?dup  if
		\ Found it
		nip nip exit
	    then			( str len )
    repeat
    2drop 2drop 0			( 0 )
;

: read-super ( sector -- )
   0 " seek" boot-ihandle $call-method -1 =  if 
      ." Seek failed" cr abort
   then
   sb-buf sbsize " read" boot-ihandle $call-method
   dup sbsize <>  if
      ." Read of superblock failed" cr
      ." requested" space sbsize .
      ." actual" space . cr
      abort
   else 
      drop
   then
;

: check-supers ( -- found? )
   \ Superblocks used to be 8KB into the partition, but ffsv2 changed that.
   \ See comments in src/sys/ufs/ffs/fs.h
   \ Put a list of offets to check on the stack, ending with -1
   -1
   0
   d# 128 KB
   d# 64 KB
   8 KB
   
   begin  dup -1 <>  while			( -1 .. off )
	 read-super				( -1 .. )
	 sb-buf fs-magic?  if			( -1 .. )
	    begin  -1 =  until	 \ Clean out extra stuff from stack
	    true exit
	 then
   repeat
   drop false
;

: ufs-open ( -- )
   boot-debug?  if ." Try superblock read" cr  then
   check-supers 0=  abort" Invalid superblock magic"
   sb-buf fs-bsize l@ dup maxbsize >  if
      ." Superblock bsize" space . ." too large" cr
      abort
   then 
   dup fs-size <  if
      ." Superblock bsize < size of superblock" cr
      abort
   then
   dup  to  cur-blocksize alloc-mem  to  cur-block    \ Allocate cur-block
   cur-blocksize alloc-mem  to  indir-block
   boot-debug?  if  ." ufs-open complete" cr  then
;

: ufs-close ( -- ) 
    cur-block 0<> if
       cur-block cur-blocksize free-mem
       indir-block cur-blocksize free-mem
    then
;

: boot-path ( -- boot-path )
    " bootpath" chosen-phandle get-package-property  if
	." Could not find bootpath in /chosen" cr
	abort
    else
	decode-string 2swap 2drop
    then
;

: boot-args ( -- boot-args )
    " bootargs" chosen-phandle get-package-property  if
	." Could not find bootargs in /chosen" cr
	abort
    else
	decode-string 2swap 2drop
    then
;

2000 buffer: boot-path-str
2000 buffer: boot-path-tmp

: split-path ( path len -- right len left len )
\ Split a string at the `/'
    begin
	dup -rot				( oldlen right len left )
	ascii / left-parse-string		( oldlen right len left len )
	dup 0<>  if  4 roll drop exit  then
	2drop					( oldlen right len )
	rot over =				( right len diff )
    until
;

: find-file ( load-file len -- )
    rootino dup sb-buf read-inode	( load-file len pino )
    -rot				( pino load-file len )
    \
    \ For each path component
    \
    begin  split-path dup 0<>  while	( pino right len left len )
	    cur-inode is-dir? not  if  ." Inode not directory" cr abort  then
	    boot-debug?  if  ." Looking for" space 2dup type space ." in directory..." cr  then
	    search-directory		( pino right len ino|false )
	    dup 0=  abort" Bad path" 	( pino right len cino )
	    sb-buf read-inode			( pino right len )
	    cur-inode is-symlink?  if		\ Symlink -- follow the damn thing
		\ Save path in boot-path-tmp
		boot-path-tmp strmov		( pino new-right len )
		
		\ Now deal with symlink  XXX drop high word of linklen
		cur-inode di-size d@ drop	( pino right len linklen.lo )
		dup sb-buf fs_maxsymlinklen l@	( pino right len linklen linklen maxlinklen )
		<  if				\ Now join the link to the path
		    0 cur-inode di-db@ drop	( pino right len linklen linkp )
		    swap boot-path-str strmov	( pino right len new-linkp linklen )
		else				\ Read file for symlink -- Ugh
		    \ Read link into boot-path-str
		    boot-path-str dup sb-buf fs-bsize l@
		    0 block-map			( pino right len linklen boot-path-str bsize blockno.lo blockno.hi )
		    strategy drop swap		( pino right len boot-path-str linklen )
		then 				( pino right len linkp linklen )
		\ Concatenate the two paths
		strcat				( pino new-right newlen )
		swap dup c@ ascii / =  if	\ go to root inode?
		    rot drop rootino -rot	( rino len right )
		then
		rot dup sb-buf read-inode	( len right pino )
		-rot swap			( pino right len )
	    then				( pino right len )
    repeat
    2drop drop
;

: .read-file-msg ( addr xxx siz -- addr xxx siz )
    boot-debug? if
	." Copying " dup . ." bytes to " 3 pick . cr
    then
;
       
: read-file ( addr size -- )
    noop \ In case we need to debug this
    \ Read x bytes from a file to buffer
    begin  dup 0>  while
	    file-offset cur-inode di-size d@ drop >  if
		." read-file EOF exceeded" cr abort
	    then
	    sb-buf buf-read-file		( addr size buf len )
	    
	    .read-file-msg
	    
	    \ Copy len bytes to addr  XXX min ( len, size ) ?
	    2over drop 3dup swap move drop	( addr size buf len )
	    
	    dup file-offset +  to  file-offset	( addr size buf len )
	    
	    nip tuck - -rot + swap		( addr' size' )
    repeat
    2drop
;

\
\ According to the 1275 addendum for SPARC processors:
\ Default load-base is 0x4000.  At least 0x8.0000 or
\ 512KB must be available at that address.  
\
\ The Fcode bootblock can take up up to 8KB (O.K., 7.5KB) 
\ so load programs at 0x4000 + 0x2000=> 0x6000
\
" load-base " evaluate 2000 + constant loader-base

: load-file-signon ( load-file len boot-path len -- load-file len boot-path len )
   ." Loading file" space 2over type cr ." from device" space 2dup type cr
;

: load-file ( load-file len -- load-base )
   
   ufs-open 				( load-file len )
   find-file				( load-file len )

    \
    \ Now we've found the file we should read it in in one big hunk
    \
    
    cur-inode di-size d@  if  ." File len >2GB!" cr abort  then
\    dup " to file-size " evaluate	( file-len ) \ Wassthis?
    boot-debug?  if
	." Loading " dup . ."  bytes of file..." cr
    then
    0  to  file-offset
    -1  to  file-blockno
    loader-base				( buf-len addr )
    tuck swap read-file			( addr )
    ufs-close				( addr )
;

0 value dev-block			\ Buffer for reading device blocks
0 value dev-blocksize			\ Size of device block buffer
-1 value dev-blockno

0 value part-type			\ Type of 'a' partition.

: read-disklabel ( )
   dev-block dev-blocksize 0 0		\ LABELSECTOR == 0
   strategy				( buf len start.lo start.hi -- nread )
   dev-blocksize <> if
      ." Failed to read disklabel" cr
      abort
   then
   dev-block sl_magic w@ dup sun_dkmagic <> if
      ." Invalid disklabel magic" space . cr
      abort
   then drop
   dev-block sl_types c@ dup to part-type
   drop
;

: is-bootable-softraid? ( -- softraid? )
   part-type fs_raid <> if false exit then

   \ XXX
   dev-block dev-blocksize sr_meta_offset 0
   strategy				( buf len block.lo block.hi -- nread )
   dev-blocksize <> if
      ." Failed to read softraid metadata" cr
      abort
   then

   dev-block ssd_magic l@ sr_magic1 <> if false exit then
   dev-block ssd_magic 4 + l@ sr_magic2 <> if false exit then

   boot-debug? if ." found softraid metadata" cr then

   \ Metadata version must be 4 or greater.
   dev-block ssd_version l@ dup 4 < if
      ." softraid version " space . space ." does not support booting" cr
      abort
   then drop

   \ Is this softraid volume bootable?
   dev-block ssd_vol_flags l@ bioc_scbootable and bioc_scbootable <> if
      ." softraid volume is not bootable" cr
      abort
   then

   true
;

: softraid-boot ( offset size -- load-base )
   boot-debug? if ." softraid-boot " 2dup . . cr then
   swap to dev-blockno
   loader-base

   \ Load boot loader from softraid boot area
   begin over 0> while
      \ XXX
      dev-block dev-blocksize dev-blockno 0
      strategy				( size addr buf len start -- nread )
      dup dev-blocksize <> if
         ." softraid-boot: block read failed" cr
         abort
      then
      dev-blockno 1 + to dev-blockno
      2dup dev-block rot rot		( size addr nread buf addr len )
      move				( size addr nread )
      dup rot +				( nread size newaddr )
      rot rot - swap			( newsize newaddr )
   repeat
   2drop

   loader-base
;

: do-boot ( bootfile -- )
   ." OpenBSD IEEE 1275 Bootblock 2.1" cr

   \ Open boot device
   boot-path				( boot-path len )
   boot-debug? if
      ." Booting from device" space 2dup type cr
   then
   drop
   cif-open dup 0= if			( ihandle? )
      ." Could not open device" space type cr
      abort
   then
   to boot-ihandle			\ Save ihandle to boot device

   \ Allocate memory for reading disk blocks
   dev_bsize dup to dev-blocksize	( blocksize )
   alloc-mem to dev-block

   \ Read disklabel
   read-disklabel

   \ Are we booting from a softraid volume?
   is-bootable-softraid? if
      sr_boot_offset sr_boot_size dev_bsize *
      softraid-boot			( blockno size -- load-base )
   else
      " /ofwboot" load-file		( -- load-base )
   then

   \ Free memory for reading disk blocks
   cur-block 0<> if
      dev-block dev-blocksize free-mem
   then

   \ Close boot device
   boot-ihandle dup -1 <> if
      cif-close -1 to boot-ihandle 
   then
   
   dup 0<> if " to load-base init-program" evaluate then
;

boot-args ascii V strchr 0<> swap drop if
   true to boot-debug?
then

boot-args ascii D strchr 0= swap drop if
   do-boot
then exit
