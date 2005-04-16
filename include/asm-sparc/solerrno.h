/* $Id: solerrno.h,v 1.5 1996/04/25 06:13:32 davem Exp $
 * solerrno.h: Solaris error return codes for compatibility.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SOLERRNO_H
#define _SPARC_SOLERRNO_H

#define SOL_EPERM          1     /* Required superuser access perms  */
#define SOL_ENOENT         2     /* File or directory does not exist */
#define SOL_ESRCH          3     /* Process did not exist            */
#define	SOL_EINTR          4     /* System call was interrupted      */
#define	SOL_EIO            5     /* An i/o error occurred            */
#define	SOL_ENXIO          6     /* Device or Address does not exist */
#define	SOL_E2BIG          7	 /* Too many arguments were given    */
#define	SOL_ENOEXEC        8     /* Header of executable was munged  */
#define	SOL_EBADF          9     /* Bogus file number                */
#define	SOL_ECHILD         10    /* No children of process exist     */
#define	SOL_EAGAIN         11    /* beep beep, "try again later"     */
#define	SOL_ENOMEM         12    /* No memory available              */
#define	SOL_EACCES         13    /* Access not allowed               */
#define	SOL_EFAULT         14    /* Address passed was invalid       */
#define	SOL_ENOTBLK        15    /* blkdev op on non-block device    */
#define	SOL_EBUSY          16    /* Mounted device was busy          */
#define	SOL_EEXIST         17    /* File specified already exists    */
#define	SOL_EXDEV          18    /* Link request across diff devices */
#define	SOL_ENODEV         19    /* Device does not exist on system  */
#define	SOL_ENOTDIR        20    /* Dir operation on non-directory   */
#define	SOL_EISDIR         21    /* File was of directory type       */
#define	SOL_EINVAL         22    /* Argument passed was invalid      */
#define	SOL_ENFILE         23    /* No more room in file table       */
#define	SOL_EMFILE         24    /* Proc has too many files open     */
#define	SOL_ENOTTY         25    /* Ioctl was invalid for req device */
#define	SOL_ETXTBSY        26    /* Text file in busy state          */
#define	SOL_EFBIG          27    /* Too big of a file for operation  */
#define	SOL_ENOSPC         28    /* Disk is full                     */
#define	SOL_ESPIPE         29    /* Seek attempted on non-seeking dev*/
#define	SOL_EROFS          30    /* Write attempted on read-only fs  */
#define	SOL_EMLINK         31    /* Too many links in file search    */
#define	SOL_EPIPE          32    /* Call a plumber                   */
#define	SOL_EDOM           33    /* Argument was out of fct domain   */
#define	SOL_ERANGE         34    /* Could not represent math result  */
#define	SOL_ENOMSG         35    /* Message of req type doesn't exist */
#define	SOL_EIDRM          36    /* Identifier has been removed      */
#define	SOL_ECHRNG         37    /* Req channel number out of range  */
#define	SOL_EL2NSYNC       38    /* Could not sync at run level 2    */
#define	SOL_EL3HLT         39    /* Halted at run level 3            */
#define	SOL_EL3RST         40    /* Reset at run level 3             */
#define	SOL_ELNRNG         41    /* Out of range link number         */
#define	SOL_EUNATCH        42    /* Driver for protocol not attached */
#define	SOL_ENOCSI         43    /* CSI structure not around         */
#define	SOL_EL2HLT         44    /* Halted at run level 2            */
#define	SOL_EDEADLK        45    /* Deadlock condition detected      */
#define	SOL_ENOLCK         46    /* Record locks unavailable         */
#define	SOL_ECANCELED      47    /* Cancellation of oper. happened   */
#define	SOL_ENOTSUP        48    /* Attempt of unsupported operation */
#define	SOL_EDQUOT         49    /* Users disk quota exceeded        */
#define	SOL_EBADE          50    /* Invalid exchange                 */
#define	SOL_EBADR          51    /* Request descriptor was invalid   */
#define	SOL_EXFULL         52    /* Full exchange                    */
#define	SOL_ENOANO         53    /* ano does not exist               */
#define	SOL_EBADRQC        54    /* Req code was invalid             */
#define	SOL_EBADSLT        55    /* Bad slot number                  */
#define	SOL_EDEADLOCK      56    /* Deadlock in fs error             */
#define	SOL_EBFONT         57    /* Font file format invalid         */
/* YOW, I LOVE SYSV STREAMS!!!! */
#define	SOL_ENOSTR         60    /* Stream-op on non-stream dev      */
#define	SOL_ENODATA        61    /* No data avail at this time       */
#define	SOL_ETIME          62    /* Expiration of time occurred      */
#define	SOL_ENOSR          63    /* Streams resources exhausted      */
#define	SOL_ENONET         64    /* No network connected             */
#define	SOL_ENOPKG         65    /* Non-installed package            */
#define	SOL_EREMOTE        66    /* Object was on remote machine     */
#define	SOL_ENOLINK        67    /* Cut link                         */
#define	SOL_EADV           68    /* Error in advertise               */
#define	SOL_ESRMNT         69    /* Some magic srmount problem       */
#define	SOL_ECOMM          70    /* During send, comm error occurred */
#define	SOL_EPROTO         71    /* Protocol botch                   */
#define	SOL_EMULTIHOP      74    /* Multihop attempted               */
#define	SOL_EBADMSG        77    /* Message was unreadable           */
#define	SOL_ENAMETOOLONG   78    /* Too long of a path name          */
#define	SOL_EOVERFLOW      79    /* Data type too small for datum    */
#define	SOL_ENOTUNIQ       80    /* Logical name was not unique      */
#define	SOL_EBADFD         81    /* Op cannot be performed on fd     */
#define	SOL_EREMCHG        82    /* Remote address is now different  */
#define	SOL_ELIBACC        83    /* Shared lib could not be accessed */
#define	SOL_ELIBBAD        84    /* ShLib is corrupted in some way   */
#define	SOL_ELIBSCN        85    /* A.out ShLib problems             */
#define	SOL_ELIBMAX        86    /* Exceeded ShLib linkage limit     */
#define	SOL_ELIBEXEC       87    /* Execution of ShLib attempted     */
#define	SOL_EILSEQ         88    /* Bad byte sequence found          */
#define	SOL_ENOSYS         89    /* Invalid filesystem operation     */
#define	SOL_ELOOP          90    /* Detected loop in symbolic links  */
#define	SOL_ERESTART       91    /* System call is restartable       */
#define	SOL_ESTRPIPE       92    /* Do not sleep in head of stream   */
#define	SOL_ENOTEMPTY      93    /* Rmdir of non-empty directory     */
#define	SOL_EUSERS         94    /* Over abundance of users for ufs  */
#define	SOL_ENOTSOCK       95    /* Sock-op on non-sock              */
#define	SOL_EDESTADDRREQ   96    /* No dest addr given, but needed   */
#define	SOL_EMSGSIZE       97    /* Msg too big                      */
#define	SOL_EPROTOTYPE     98    /* Bad socket protocol              */
#define	SOL_ENOPROTOOPT    99    /* Unavailable protocol             */
#define	SOL_EPROTONOSUPPORT 120  /* Unsupported protocol             */
#define	SOL_ESOCKTNOSUPPORT 121  /* Unsupported socket type          */
#define	SOL_EOPNOTSUPP     122   /* Unsupported sock-op              */
#define	SOL_EPFNOSUPPORT   123   /* Unsupported protocol family      */
#define	SOL_EAFNOSUPPORT   124   /* Unsup addr family for protocol   */
#define	SOL_EADDRINUSE     125   /* Req addr is already in use       */
#define	SOL_EADDRNOTAVAIL  126   /* Req addr not available right now */
#define	SOL_ENETDOWN       127   /* Your subnet is on fire           */
#define	SOL_ENETUNREACH    128   /* Someone playing with gateway and */
                                 /* did not tell you he was going to */
#define	SOL_ENETRESET      129   /* Buy less-buggy ethernet cards    */
#define	SOL_ECONNABORTED   130   /* Aborted connection due to sw     */
#define	SOL_ECONNRESET     131   /* Your peers reset your connection */
#define	SOL_ENOBUFS        132   /* No buffer space available        */
#define	SOL_EISCONN        133   /* Connect on already connected     */
                                 /* socket attempted                 */
#define	SOL_ENOTCONN       134   /* Comm on non-connected socket     */
#define	SOL_ESHUTDOWN      143   /* Op attempted after sock-shutdown */
#define	SOL_ETOOMANYREFS   144   /* Reference limit exceeded         */
#define	SOL_ETIMEDOUT      145   /* Timed out connection             */
#define	SOL_ECONNREFUSED   146   /* Connection refused by remote host*/
#define	SOL_EHOSTDOWN      147   /* Remote host is up in flames      */
#define	SOL_EHOSTUNREACH   148   /* Make a left at Easton Ave.....   */
#define	SOL_EWOULDBLOCK    EAGAIN /* Just an alias */
#define	SOL_EALREADY       149   /* Operation is already occurring   */
#define	SOL_EINPROGRESS    150   /* Operation is happening now       */
#define	SOL_ESTALE         151   /* Fungus growth on NFS file handle */

#endif /* !(_SPARC_SOLERRNO_H) */
