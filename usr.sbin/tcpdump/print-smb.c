/*	$OpenBSD: print-smb.c,v 1.5 2017/05/30 20:10:45 deraadt Exp $	*/

/*
   Copyright (C) Andrew Tridgell 1995-1999

   This software may be distributed either under the terms of the
   BSD-style license that accompanies tcpdump or the GNU GPL version 2
   or later */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "interface.h"
#include "smb.h"

static int request=0;

const uchar *startbuf=NULL;

struct smbdescript
{
  char *req_f1;
  char *req_f2;
  char *rep_f1;
  char *rep_f2;
  void (*fn)(); /* sometimes (u_char *, u_char *, u_char *, u_char *)
		and sometimes (u_char *, u_char *, int, int) */
};

struct smbfns
{
  int id;
  char *name;
  int flags;
  struct smbdescript descript;
};

#define DEFDESCRIPT  {NULL,NULL,NULL,NULL,NULL}

#define FLG_CHAIN (1<<0)

static struct smbfns *smbfind(int id,struct smbfns *list)
{
  int sindex;

  for (sindex=0;list[sindex].name;sindex++)
    if (list[sindex].id == id) return(&list[sindex]);

  return(&list[0]);
}

static void trans2_findfirst(uchar *param,uchar *data,int pcnt,int dcnt)
{
  char *fmt;

  if (request) {
    fmt = "attr [A] searchcnt [d] flags [w] level [dP5] file [S] ";
  } else {
    fmt = "handle [w] cnt [d] eos [w] eoffset [d] lastnameofs [w] ";
  }

  fdata(param,fmt,param+pcnt);
}

static void trans2_qfsinfo(uchar *param,uchar *data,int pcnt,int dcnt)
{
  static int level=0;
  char *fmt="";

  if (request) {
    level = SVAL(param,0);
    fmt = "info level [d] ";
    fdata(param,fmt,param+pcnt);
  } else {
    switch (level) {
    case 1:
      fmt = "fsid [W] sectorunit [D] unit [D] avail [D] sectorsize [d] ";
      break;
    case 2:
      fmt = "creat [T2] volnamelen [B] volume [s12] ";
      break;
    case 0x105:
      fmt = "capabilities [W] maxfilelen [D] volnamelen [D] volume [S] ";
      break;
    default:
      fmt = "unknown level ";
    }
    fdata(data,fmt,data+dcnt);
  }
}

struct smbfns trans2_fns[] = {
{0,"TRANSACT2_OPEN",0,
   {"flags2 [w] mode [w] searchattr [A] attr [A] time [T2] ofun [w] size [D] res [w,w,w,w,w] path [S]",NULL,
    "handle [d] attr [A] time [T2] size [D] access [w] type [w] state [w] action [w] inode [W] offerr [d] |ealen [d] ",NULL,NULL}},

{1,"TRANSACT2_FINDFIRST",0,
   {NULL,NULL,NULL,NULL,trans2_findfirst}},

{2,"TRANSACT2_FINDNEXT",0,DEFDESCRIPT},

{3,"TRANSACT2_QFSINFO",0,
   {NULL,NULL,NULL,NULL,trans2_qfsinfo}},

{4,"TRANSACT2_SETFSINFO",0,DEFDESCRIPT},
{5,"TRANSACT2_QPATHINFO",0,DEFDESCRIPT},
{6,"TRANSACT2_SETPATHINFO",0,DEFDESCRIPT},
{7,"TRANSACT2_QFILEINFO",0,DEFDESCRIPT},
{8,"TRANSACT2_SETFILEINFO",0,DEFDESCRIPT},
{9,"TRANSACT2_FSCTL",0,DEFDESCRIPT},
{10,"TRANSACT2_IOCTL",0,DEFDESCRIPT},
{11,"TRANSACT2_FINDNOTIFYFIRST",0,DEFDESCRIPT},
{12,"TRANSACT2_FINDNOTIFYNEXT",0,DEFDESCRIPT},
{13,"TRANSACT2_MKDIR",0,DEFDESCRIPT},
{-1,NULL,0,DEFDESCRIPT}};


static void print_trans2(uchar *words,uchar *dat,uchar *buf,uchar *maxbuf)
{
  static struct smbfns *fn = &trans2_fns[0];
  uchar *data,*param;
  uchar *f1=NULL,*f2=NULL;
  int pcnt,dcnt;

  if (request) {
    fn = smbfind(SVAL(words+1,14*2),trans2_fns);
    data = buf+SVAL(words+1,12*2);
    param = buf+SVAL(words+1,10*2);
    pcnt = SVAL(words+1,9*2);
    dcnt = SVAL(words+1,11*2);
  } else {
    data = buf+SVAL(words+1,7*2);
    param = buf+SVAL(words+1,4*2);
    pcnt = SVAL(words+1,3*2);
    dcnt = SVAL(words+1,6*2);
  }

  printf("%s paramlen %d datalen %d ",
	 fn->name,pcnt,dcnt);

  if (request) {
    if (CVAL(words,0) == 8) {
      fdata(words+1,"trans2secondary totparam [d] totdata [d] paramcnt [d] paramoff [d] paramdisp [d] datacnt [d] dataoff [d] datadisp [d] handle [d] ",maxbuf);
      return;
    } else {
      fdata(words+1,"totparam [d] totdata [d] maxparam [d] maxdata [d] maxsetup [d] flags [w] timeout [D] res1 [w] paramcnt [d] paramoff [d] datacnt=[d] dataoff [d] setupcnt [d] ",words+1+14*2);
      fdata(data+1,"transname [S] %",maxbuf);
    }
    f1 = fn->descript.req_f1;
    f2 = fn->descript.req_f2;
  } else {
    if (CVAL(words,0) == 0) {
      printf("trans2interim ");
      return;
    } else {
      fdata(words+1,"totparam [d] totdata [d] res1 [w] paramcnt [d] paramoff [d] paramdisp [d] datacnt [d] dataoff [d] datadisp [d] setupcnt [d] ",words+1+10*2);
    }
    f1 = fn->descript.rep_f1;
    f2 = fn->descript.rep_f2;
  }

  if (fn->descript.fn) {
    fn->descript.fn(param,data,pcnt,dcnt);
  } else {
    fdata(param,f1?f1:(uchar*)"params ",param+pcnt);
    fdata(data,f2?f2:(uchar*)"data ",data+dcnt);
  }
}


static void print_browse(uchar *param,int paramlen,const uchar *data,int datalen)
{
  const uchar *maxbuf = data + datalen;
  int command = CVAL(data,0);

  fdata(param,"browse |param ",param+paramlen);

  switch (command) {
  case 0xF:
    data = fdata(data,"browse [B] (LocalMasterAnnouncement) updatecnt [w] res1 [B] announceintv [d] name [n2] version [B].[B] servertype [W] electionversion [w] browserconst [w] ",maxbuf);
    break;

  case 0x1:
    data = fdata(data,"browse [B] (HostAnnouncement) updatecnt [w] res1 [B] announceintv [d] name [n2] version [B].[B] servertype [W] electionversion [w] browserconst [w] ",maxbuf);
    break;

  case 0x2:
    data = fdata(data,"browse [B] (AnnouncementRequest) flags [B] replysysname [S] ",maxbuf);
    break;

  case 0xc:
    data = fdata(data,"browse [B] (WorkgroupAnnouncement) updatecnt [w] res1 [B] announceintv [d] name [n2] version [B].[B] servertype [W] commentptr [W] servername [S] ",maxbuf);
    break;

  case 0x8:
    data = fdata(data,"browse [B] (ElectionFrame) electionversion [B] ossummary [W] uptime [(W,W)] servername [S] ",maxbuf);
    break;

  case 0xb:
    data = fdata(data,"browse [B] (BecomeBackupBrowser) name [S] ",maxbuf);
    break;

  case 0x9:
    data = fdata(data,"browse [B] (GetBackupList) listcnt? [B] token? [B] ",maxbuf);
    break;

  case 0xa:
    data = fdata(data,"browse [B] (BackupListResponse) servercnt? [B] token? [B] *name [S] ",maxbuf);
    break;

  case 0xd:
    data = fdata(data,"browse [B] (MasterAnnouncement) master-name [S] ",maxbuf);
    break;

  case 0xe:
    data = fdata(data,"browse [B] (ResetBrowser) options [B] ",maxbuf);
    break;

  default:
    data = fdata(data,"browse unknown-frame",maxbuf);
    break;
  }
}


static void print_ipc(uchar *param,int paramlen,uchar *data,int datalen)
{
  if (paramlen)
    fdata(param,"cmd [w] str1 [S] str2 [S] ",param+paramlen);
  if (datalen)
    fdata(data,"IPC ",data+datalen);
}


static void print_trans(uchar *words,uchar *data1,uchar *buf,uchar *maxbuf)
{
  uchar *f1,*f2,*f3,*f4;
  uchar *data,*param;
  int datalen,paramlen;

  if (request) {
    paramlen = SVAL(words+1,9*2);
    param = buf + SVAL(words+1,10*2);
    datalen = SVAL(words+1,11*2);
    data = buf + SVAL(words+1,12*2);
    f1 = " totparamcnt [d] totdatacnt [d] maxparmcnt [d] maxdatacnt [d] maxscnt [d] transflags [w] res [w] [w] [w] paramcnt [d] paramoff [d] datacnt [d] dataoff [d] sucnt [d] ";
    f2 = "|[S] ";
    f3 = "|param ";
    f4 = "|data ";
  } else {
    paramlen = SVAL(words+1,3*2);
    param = buf + SVAL(words+1,4*2);
    datalen = SVAL(words+1,6*2);
    data = buf + SVAL(words+1,7*2);
    f1 = "totparamcnt [d] totdatacnt [d] res1 [d] paramcnt [d] paramoff [d] res2 [d] datacnt [d] dataoff [d] res3 [d] Lsetup [d] ";
    f2 = "|unk ";
    f3 = "|param ";
    f4 = "|data ";
  }

  fdata(words+1,f1,MINIMUM(words+1+2*CVAL(words,0),maxbuf));
  fdata(data1+2,f2,maxbuf - (paramlen + datalen));

  if (!strcmp(data1+2,"\\MAILSLOT\\BROWSE")) {
    print_browse(param,paramlen,data,datalen);
    return;
  }

  if (!strcmp(data1+2,"\\PIPE\\LANMAN")) {
    print_ipc(param,paramlen,data,datalen);
    return;
  }

  if (paramlen) fdata(param,f3,MINIMUM(param+paramlen,maxbuf));
  if (datalen) fdata(data,f4,MINIMUM(data+datalen,maxbuf));
}



static void print_negprot(uchar *words,uchar *data,uchar *buf,uchar *maxbuf)
{
  uchar *f1=NULL,*f2=NULL;

  if (request) {
    f2 = "*|dialect [Z] ";
  } else {
    if (CVAL(words,0) == 1) {
      f1 = "core-proto dialect index [d]";
    } else if (CVAL(words,0) == 17) {
      f1 =  "NT1-proto dialect index [d] secmode [B] maxmux [d] numvcs [d] maxbuf [D] rawsize [D] sesskey [W] capabilities [W] servertime [T3] tz [d] cryptkey ";
    } else if (CVAL(words,0) == 13) {
      f1 = "coreplus/lanman1/lanman2-proto dialect index [d] secmode [w] maxxmit [d] maxmux [d] maxvcs [d] blkmode [w] sesskey [W] servertime [T1] tz [d] res [W] cryptkey ";
    }
  }

  if (f1)
    fdata(words+1,f1,MINIMUM(words + 1 + CVAL(words,0)*2,maxbuf));

  if (f2)
    fdata(data+2,f2,MINIMUM(data + 2 + SVAL(data,0),maxbuf));
}

static void print_sesssetup(uchar *words,uchar *data,uchar *buf,uchar *maxbuf)
{
  int wcnt = CVAL(words,0);
  uchar *f1=NULL,*f2=NULL;

  if (request) {
    if (wcnt==10) {
      f1 = "com2 [w] off2 [d] bufsize [d] maxmpx [d] vcnum [d] sesskey [W] passlen [d] cryptlen [d] cryptoff [d] pass&name  ";
    } else {
      f1 = "com2 [B] res1 [B] off2 [d] maxbuf [d] maxmpx [d] vcnum [d] sesskey [W] case-insensitive-passlen [d] case-sensitive-passlen [d] res [W] capabilities [W] pass1&pass2&account&domain&os&lanman  ";
    }
  } else {
    if (CVAL(words,0) == 3) {
      f1 = "com2 [w] off2 [d] action [w] ";
    } else if (CVAL(words,0) == 13) {
      f1 = "com2 [B] res [B] off2 [d] action [w] ";
      f2 = "native-os [S] nativelanman [S] primarydomain [S] ";
    }
  }

  if (f1)
    fdata(words+1,f1,MINIMUM(words + 1 + CVAL(words,0)*2,maxbuf));

  if (f2)
    fdata(data+2,f2,MINIMUM(data + 2 + SVAL(data,0),maxbuf));
}


static struct smbfns smb_fns[] =
{
{-1,"SMBunknown",0,DEFDESCRIPT},

{SMBtcon,"SMBtcon",0,
   {NULL,"path [Z] pass [Z] dev [Z] ", "xmitmax [d] treeid [d] ",NULL, NULL}},


{SMBtdis,"SMBtdis",0,DEFDESCRIPT},
{SMBexit,"SMBexit",0,DEFDESCRIPT},
{SMBioctl,"SMBioctl",0,DEFDESCRIPT},

{SMBecho,"SMBecho",0,
   {"reverbcount [d] ",NULL, "seqnum [d] ",NULL, NULL}},

{SMBulogoffX, "SMBulogoffX",FLG_CHAIN,DEFDESCRIPT},

{SMBgetatr,"SMBgetatr",0, {NULL,"path [Z] ",
    "attr [A] time [T2] size [D] res ([w,w,w,w,w]) ",NULL, NULL}},

{SMBsetatr,"SMBsetatr",0,
   {"attr [A] time [T2] res ([w,w,w,w,w]) ","path [Z] ", NULL,NULL,NULL}},

{SMBchkpth,"SMBchkpth",0, {NULL,"path [Z] ",NULL,NULL,NULL}},

{SMBsearch,"SMBsearch",0,
{"cnt [d] attr [A] ","path [Z] blktype [B] blklen [d] |res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] res2 [W] ",
"cnt [d] ","blktype [B] blklen [d] * res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] res2 [W] attr [a] time [T1] size [D] name [s13] ",NULL}},

{SMBopen,"SMBopen",0, {"mode [w] attr [A] ","path [Z] ", "handle [d] oattr [A] time [T2] size [D] access [w] ",NULL, NULL}},

{SMBcreate,"SMBcreate",0, {"attr [A] time [T2]","path [Z] ", "handle [d]",NULL, NULL}},

{SMBmknew,"SMBmknew",0, {"attr [A] time [T2]","path [Z] ", "handle [d] ",NULL, NULL}},

{SMBunlink,"SMBunlink",0, {"attr [A] ","path [Z] ",NULL,NULL,NULL}},

{SMBread,"SMBread",0, {"handle [d] bytecnt [d] offset [D] cntleft [d] ",NULL, "cnt [d] res ([w,w,w,w]) ",NULL,NULL}},

{SMBwrite,"SMBwrite",0, {"handle [d] bytecnt [d] offset [D] cntleft [d] ",NULL, "cnt [d] ",NULL,NULL}},

{SMBclose,"SMBclose",0, {"handle [d] time [T2]",NULL,NULL,NULL,NULL}},

{SMBmkdir,"SMBmkdir",0, {NULL,"path [Z] ",NULL,NULL,NULL}},

{SMBrmdir,"SMBrmdir",0, {NULL,"path [Z] ",NULL,NULL,NULL}},

{SMBdskattr,"SMBdskattr",0, {NULL,NULL, "totalunits [d] blks/unit [d] blksize [d] freeunits [d] media [w] ", NULL,NULL}},

{SMBmv,"SMBmv",0, {"attr [A] ","oldpath [Z] newpath [Z] ",NULL,NULL,NULL}},

/* this is a Pathworks specific call, allowing the
   changing of the root path */
{pSETDIR,"SMBsetdir",0, {NULL,"path [Z] ",NULL,NULL,NULL}},

{SMBlseek,"SMBlseek",0, {"handle [d] mode [w] offset [D] ","offset [D] ",NULL,NULL}},

{SMBflush,"SMBflush",0, {"handle [d] ",NULL,NULL,NULL,NULL}},

{SMBsplopen,"SMBsplopen",0, {"setuplen [d] mode [w] ","ident [Z] ","handle [d] ",NULL,NULL}},

{SMBsplclose,"SMBsplclose",0, {"handle [d] ",NULL,NULL,NULL,NULL}},

{SMBsplretq,"SMBsplretq",0, {"maxcnt [d] startindex [d] ",NULL, "cnt [d] index [d] ", "*time [T2] status [B] jobid [d] size [D] res [B] name [s16] ", NULL}},

{SMBsplwr,"SMBsplwr",0, {"handle [d] ",NULL,NULL,NULL,NULL}},

{SMBlock,"SMBlock",0, {"handle [d] count [D] offset [D] ",NULL,NULL,NULL,NULL}},

{SMBunlock,"SMBunlock",0, {"handle [d] count [D] offset [D] ",NULL,NULL,NULL,NULL}},

/* CORE+ PROTOCOL FOLLOWS */

{SMBreadbraw,"SMBreadbraw",0, {"handle [d] offset [D] maxcnt [d] mincnt [d] timeout [D] res [d] ", NULL,NULL,NULL,NULL}},

{SMBwritebraw,"SMBwritebraw",0, {"handle [d] totalcnt [d] res [w] offset [D] timeout [D] wmode [w] res2 [W] |datasize [d] dataoff [d] ", NULL,"write-raw-ack",NULL,NULL}},

{SMBwritec,"SMBwritec",0, {NULL,NULL,"count [d] ",NULL,NULL}},

{SMBwriteclose,"SMBwriteclose",0, {"handle [d] count [d] offset [D] time [T2] res ([w,w,w,w,w,w])",NULL, "count [d] ",NULL,NULL}},

{SMBlockread,"SMBlockread",0, {"handle [d] bytecnt [d] offset [D] cntleft [d] ",NULL, "count [d] res ([w,w,w,w]) ",NULL,NULL}},

{SMBwriteunlock,"SMBwriteunlock",0, {"handle [d] bytecnt [d] offset [D] cntleft [d] ",NULL, "count [d] ",NULL,NULL}},

{SMBreadBmpx,"SMBreadBmpx",0, {"handle [d] offset [D] maxcnt [d] mincnt [d] timeout [D] res [w] ", NULL, "offset [D] totcnt [d] remain [d] res ([w,w]) datasize [d] dataoff [d] ", NULL,NULL}},

{SMBwriteBmpx,"SMBwriteBmpx",0, {"handle [d] totcnt [d] res [w] offset [D] timeout [D] wmode [w] res2 [W] datasize [d] dataoff [d] ",NULL, "remain [d] ",NULL,NULL}},

{SMBwriteBs,"SMBwriteBs",0, {"handle [d] totcnt [d] offset [D] res [W] datasize [d] dataoff [d] ",NULL, "count [d] ",NULL,NULL}},

{SMBsetattrE,"SMBsetattrE",0, {"handle [d] ctime [T2] atime [T2] mtime [T2]",NULL, NULL,NULL,NULL}},

{SMBgetattrE,"SMBgetattrE",0, {"handle [d] ",NULL, "ctime [T2] atime [T2] mtime [T2] size [D] allocsize [D] attr [A] ",NULL,NULL}},

{SMBtranss,"SMBtranss",0,DEFDESCRIPT},
{SMBioctls,"SMBioctls",0,DEFDESCRIPT},

{SMBcopy,"SMBcopy",0, {"treeid2 [d] ofun [w] flags [w] ","path [S] newpath [S] ", "copycnt [d] ","|errstr [S] ",NULL}},

{SMBmove,"SMBmove",0, {"treeid2 [d] ofun [w] flags [w] ","path [S] newpath [S] ", "movecnt [d] ","|errstr [S] ",NULL}},

{SMBopenX,"SMBopenX",FLG_CHAIN, {"com2 [w] off2 [d] flags [w] mode [w] searchattr [A] attr [A] time [T2] ofun [w] size [D] timeout [D] res [W] ","path [S] ", "com2 [w] off2 [d] handle [d] attr [A] time [T2] size [D] access [w] type [w] state [w] action [w] fileid [W] res [w] ",NULL,NULL}},

{SMBreadX,"SMBreadX",FLG_CHAIN, {"com2 [w] off2 [d] handle [d] offset [D] maxcnt [d] mincnt [d] timeout [D] cntleft [d] ",NULL, "com2 [w] off2 [d] remain [d] res [W] datasize [d] dataoff [d] res ([w,w,w,w]) ",NULL,NULL}},

{SMBwriteX,"SMBwriteX",FLG_CHAIN, {"com2 [w] off2 [d] handle [d] offset [D] timeout [D] wmode [w] cntleft [d] res [w] datasize [d] dataoff [d] ",NULL, "com2 [w] off2 [d] count [d] remain [d] res [W] ",NULL,NULL}},

{SMBlockingX,"SMBlockingX",FLG_CHAIN, {"com2 [w] off2 [d] handle [d] locktype [w] timeout [D] unlockcnt [d] lockcnt [d] ", "*process [d] offset [D] len [D] ", "com2 [w] off2 [d] "}},

{SMBffirst,"SMBffirst",0, {"count [d] attr [A] ","path [Z] blktype [B] blklen [d] |res1 [B] mask [s11] srv2 [B] dirindex [d] srv2 [w] ", "count [d] ","blktype [B] blklen [d] * res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] res2 [W] attr [a] time [T1] size [D] name [s13] ",NULL}},

{SMBfunique,"SMBfunique",0, {"count [d] attr [A] ","path [Z] blktype [B] blklen [d] |res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] ", "count [d] ","blktype [B] blklen [d] * res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] res2 [W] attr [a] time [T1] size [D] name [s13] ",NULL}},

{SMBfclose,"SMBfclose",0, {"count [d] attr [A] ","path [Z] blktype [B] blklen [d] |res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] ", "count [d] ","blktype [B] blklen [d] * res1 [B] mask [s11] srv1 [B] dirindex [d] srv2 [w] res2 [W] attr [a] time [T1] size [D] name [s13] ",NULL}},

{SMBfindnclose, "SMBfindnclose", 0, {"handle [d] ",NULL,NULL,NULL,NULL}},

{SMBfindclose, "SMBfindclose", 0, {"handle [d] ",NULL,NULL,NULL,NULL}},

{SMBsends,"SMBsends",0, {NULL,"src [Z] dst [Z] ",NULL,NULL,NULL}},

{SMBsendstrt,"SMBsendstrt",0, {NULL,"src [Z] dst [Z] ","groupid [d] ",NULL,NULL}},

{SMBsendend,"SMBsendend",0, {"groupid [d] ",NULL,NULL,NULL,NULL}},

{SMBsendtxt,"SMBsendtxt",0, {"groupid [d] ",NULL,NULL,NULL,NULL}},

{SMBsendb,"SMBsendb",0, {NULL,"src [Z] dst [Z] ",NULL,NULL,NULL}},

{SMBfwdname,"SMBfwdname",0,DEFDESCRIPT},
{SMBcancelf,"SMBcancelf",0,DEFDESCRIPT},
{SMBgetmac,"SMBgetmac",0,DEFDESCRIPT},

{SMBnegprot,"SMBnegprot",0, {NULL,NULL,NULL,NULL,print_negprot}},

{SMBsesssetupX,"SMBsesssetupX",FLG_CHAIN,{NULL,NULL,NULL,NULL,print_sesssetup}},

{SMBtconX,"SMBtconX",FLG_CHAIN, {"com2 [w] off2 [d] flags [w] passlen [d] passwd&path&dev  ",NULL, "com2 [w] off2 [d] ","servicetype [S] ",NULL}},

{SMBtrans2, "SMBtrans2",0,{NULL,NULL,NULL,NULL,print_trans2}},

{SMBtranss2, "SMBtranss2", 0,DEFDESCRIPT},
{SMBctemp,"SMBctemp",0,DEFDESCRIPT},
{SMBreadBs,"SMBreadBs",0,DEFDESCRIPT},
{SMBtrans,"SMBtrans",0,{NULL,NULL,NULL,NULL,print_trans}},

{SMBnttrans,"SMBnttrans", 0, DEFDESCRIPT},
{SMBnttranss,"SMBnttranss", 0, DEFDESCRIPT},

{SMBntcreateX,"SMBntcreateX", FLG_CHAIN, {"com2 [w] off2 [d] res [b] namelen [d] flags [W] rootdirfid [D] accessmask [W] allocsize [L] extfileattr [W] shareaccess [W] createdisposition [W] createopts [W] impersonallevel [W] securityflags [b] ","path [S] ", "com2 [w] off2 [d] oplocklvl [b] fid [d] createaction [W] createtime [T3] lastaccesstime [T3] lastwritetime [T3] ctime [T3]extfileattr [W] allocsize [L] eof [L] filetype [w] devstate [w] dir [b] ", NULL}},

{SMBntcancel,"SMBntcancel", 0, DEFDESCRIPT},

{-1,NULL,0,DEFDESCRIPT}};


/*******************************************************************
print a SMB message
********************************************************************/
static void print_smb(const uchar *buf, const uchar *maxbuf)
{
  int command;
  const uchar *words, *data;
  struct smbfns *fn;
  char *fmt_smbheader =
"[P4] cmd [B] error [BP1]/[d] flags [B] [B][P13] treeid [d] procid [d] uid [d] mid [d] wordcnt [b] ";

  request = (CVAL(buf,9)&0x80)?0:1;

  command = CVAL(buf,4);

  fn = smbfind(command,smb_fns);

  printf("%s-%s",fn->name,request?"request":"reply");

  if (vflag == 0) return;

  /* print out the header */
  fdata(buf,fmt_smbheader,buf+33);

  if (CVAL(buf,5)) {
    int class = CVAL(buf,5);
    int num = SVAL(buf,7);
    printf("SMBError %s ",smb_errstr(class,num));
  }

  words = buf+32;
  data = words + 1 + CVAL(words,0)*2;


  while (words && data)
    {
      char *f1,*f2;
      int wct = CVAL(words,0);

      if (request) {
	f1 = fn->descript.req_f1;
	f2 = fn->descript.req_f2;
      } else {
	f1 = fn->descript.rep_f1;
	f2 = fn->descript.rep_f2;
      }

      if (fn->descript.fn) {
	fn->descript.fn(words,data,buf,maxbuf);
      } else {
	if (f1) {
	  printf("smbvwv[]=");
	  fdata(words+1,f1,words + 1 + wct*2);
	} else if (wct) {
	  int i;
	  int v;
	  printf("smbvwv[]=");
	  for (i=0;i<wct;i++) {
	    v = SVAL(words+1,2*i);
	    printf("smb_vwv[%d]=%d (0x%X) ",i,v,v);
	  }
	}

	if (f2) {
	  printf("smbbuf[]=");
	  fdata(data+2,f2,maxbuf);
	} else {
	  int bcc = SVAL(data,0);
	  printf("smb_bcc=%d",bcc);
	}
      }

      if ((fn->flags & FLG_CHAIN) && CVAL(words,0) && SVAL(words,1)!=0xFF) {
	command = SVAL(words,1);
	words = buf + SVAL(words,3);
	data = words + 1 + CVAL(words,0)*2;

	fn = smbfind(command,smb_fns);

	printf("chained-%s-%s ",fn->name,request?"request":"reply");
      } else {
	words = data = NULL;
      }
    }
}


/*
   print a NBT packet received across tcp on port 139
*/
void nbt_tcp_print(const uchar *data,int length)
{
  const uchar *maxbuf = data + length;
  int flags = CVAL(data,0);
  int nbt_len = RSVAL(data,2);

  startbuf = data;
  if (maxbuf <= data) return;

  printf(": nbt ");

  switch (flags) {
  case 1:
    printf("flags 0x%x ", flags);
  case 0:
    data = fdata(data,"session flags [rw] len [rd] ",data+4);
    if (data == NULL)
      break;
    if (memcmp(data,"\377SMB",4)==0) {
      if (nbt_len>PTR_DIFF(maxbuf,data))
	printf("[|nbt]");
      print_smb(data,maxbuf>data+nbt_len?data+nbt_len:maxbuf);
    } else {
	    printf("session packet :(raw data?) ");
    }
    break;

  case 0x81:
    data = fdata(data,"session-request flags [rW] dst [n1] src [n1] ",maxbuf);
    break;

  case 0x82:
    data = fdata(data,"sessionr-granted flags [rW] ",maxbuf);
    break;

  case 0x83:
    {
      int ecode = CVAL(data,4);
      data = fdata(data,"session-reject flags [rW] reason [B] ",maxbuf);
      switch (ecode) {
      case 0x80:
	printf("(Not listening on called name) ");
	break;
      case 0x81:
	printf("(Not listening for calling name) ");
	break;
      case 0x82:
	printf("(Called name not present) ");
	break;
      case 0x83:
	printf("(Insufficient resources) ");
	break;
      default:
	printf("(Unspecified error 0x%X) ",ecode);
	break;
      }
    }
    break;

  case 0x85:
    data = fdata(data,"keepalive flags [rW] ",maxbuf);
    break;

  default:
    printf("flags=0x%x ", flags);
    data = fdata(data,"unknown packet type [rW] ",maxbuf);
  }
  fflush(stdout);
}


/*
   print a NBT packet received across udp on port 137
*/
void nbt_udp137_print(const uchar *data, int length)
{
  const uchar *maxbuf = data + length;
  int name_trn_id = RSVAL(data,0);
  int response = (CVAL(data,2)>>7);
  int opcode = (CVAL(data,2) >> 3) & 0xF;
  int nm_flags = ((CVAL(data,2) & 0x7) << 4) + (CVAL(data,3)>>4);
  int rcode = CVAL(data,3) & 0xF;
  int qdcount = RSVAL(data,4);
  int ancount = RSVAL(data,6);
  int nscount = RSVAL(data,8);
  int arcount = RSVAL(data,10);
  char *opcodestr;
  const char *p;

  startbuf = data;

  if (maxbuf <= data) return;

  switch (opcode) {
  case 0: opcodestr = "query"; break;
  case 5: opcodestr = "registration"; break;
  case 6: opcodestr = "release"; break;
  case 7: opcodestr = "wack"; break;
  case 8: opcodestr = "refresh(8)"; break;
  case 9: opcodestr = "refresh"; break;
  default: opcodestr = "unknown"; break;
  }
  printf("nbt-%s", opcodestr);
  if (response) {
    if (rcode)
      printf("-negative");
    else
      printf("-positive");
    printf("-resp");
  } else
    printf("-req");

  if (nm_flags&1)
    printf("-bcast");

  if (vflag == 0) return;

  printf(" transid 0x%X opcode %d nmflags 0x%X rcode %d querycnt %d answercnt %d authoritycnt %d addrreccnt %d ", name_trn_id,opcode,nm_flags,rcode,qdcount,ancount,nscount,arcount);

  p = data + 12;

  {
    int total = ancount+nscount+arcount;
    int i;

    if (qdcount>100 || total>100) {
      printf("(corrupt packet?) ");
      return;
    }

    if (qdcount) {
      printf("question: ");
      for (i=0;i<qdcount;i++)
	p = fdata(p,"|name [n1] type [rw] class [rw] #",maxbuf);
	if (p == NULL)
	  return;
    }

    if (total) {
      printf("rr: ");
      for (i=0;i<total;i++) {
	int rdlen;
	int restype;
	p = fdata(p,"name [n1] #",maxbuf);
	if (p == NULL)
	  return;
	restype = RSVAL(p,0);
	p = fdata(p,"type [rw] class [rw] ttl [rD] ",p+8);
	if (p == NULL)
	  return;
	rdlen = RSVAL(p,0);
	printf("len %d data ",rdlen);
	p += 2;
	if (rdlen == 6) {
	  p = fdata(p,"addrtype [rw] addr [b.b.b.b] ",p+rdlen);
	  if (p == NULL)
	    return;
	} else {
	  if (restype == 0x21) {
	    int numnames = CVAL(p,0);
	    p = fdata(p,"numnames [B] ",p+1);
	    if (p == NULL)
	      return;
	    while (numnames--) {
	      p = fdata(p,"name [n2] #",maxbuf);
	      if (p[0] & 0x80) printf("<GROUP> ");
	      switch (p[0] & 0x60) {
	      case 0x00: printf("B "); break;
	      case 0x20: printf("P "); break;
	      case 0x40: printf("M "); break;
	      case 0x60: printf("_ "); break;
	      }
	      if (p[0] & 0x10) printf("<DEREGISTERING> ");
	      if (p[0] & 0x08) printf("<CONFLICT> ");
	      if (p[0] & 0x04) printf("<ACTIVE> ");
	      if (p[0] & 0x02) printf("<PERMANENT> ");
	      p += 2;
	    }
	  } else
	    p += rdlen;
	}
      }
    }
  }

  if ((uchar*)p < maxbuf) {
    fdata(p,"extra: ",maxbuf);
  }

  fflush(stdout);
}



/*
   print a NBT packet received across udp on port 138
*/
void nbt_udp138_print(const uchar *data, int length)
{
  const uchar *maxbuf = data + length;
  startbuf = data;
  if (maxbuf <= data) return;

  /* EMF - figure out how to skip fields inside maxbuf easily, IP and PORT here are bloody redundant */
  data = fdata(data,"nbt res [rw] id [rw] ip [b.b.b.b] port [rd] len [rd] res2 [rw] srcname [n1] dstname [n1] #",maxbuf);

  if (data != NULL)
    print_smb(data,maxbuf);

  fflush(stdout);
}



/*
   print netbeui frames
*/
void netbeui_print(u_short control, const uchar *data, const uchar *maxbuf)
{
  int len = SVAL(data,0);
  int command = CVAL(data,4);
  const uchar *data2 = data + len;
  int is_truncated = 0;

  if (data2 >= maxbuf) {
    data2 = maxbuf;
    is_truncated = 1;
  }

  startbuf = data;

  printf("NetBeui type 0x%X ", control);
  data = fdata(data,"len [d] signature [w] cmd [B] #",maxbuf);
  if (data == NULL)
    return;

  switch (command) {
  case 0xA:
    data = fdata(data,"namequery [P1] sessnum [B] nametype [B][P2] respcorrelator [w] dst [n2] src [n2] ",data2);
    break;

  case 0x8:
    data = fdata(data,"netbios dgram [P7] dst [n2] src [n2] ",data2);
    break;

  case 0xE:
    data = fdata(data,"namerecognize [P1] data2 [w] xmitcorrelator [w] respcorrelator [w] dst [n2] src [n2] ",data2);
    break;

  case 0x19:
    data = fdata(data,"sessinit data1 [B] data2 [w] xmitcorrelator [w] respcorrelator [w] remsessnum [B] lclsessnum [B] ",data2);
    break;

  case 0x17:
    data = fdata(data,"sessconf data1 [B] data2 [w] xmitcorrelator [w] respcorrelator [w] remsessnum [B] lclsessnum [B] ",data2);
    break;

  case 0x16:
    data = fdata(data,"netbios data only last flags [{|NO_ACK|PIGGYBACK_ACK_ALLOWED|PIGGYBACK_ACK_INCLUDED|}] resyncindicator [w][P2] respcorrelator [w] remsessnum [B] lclsessnum [B] ",data2);
    break;

  case 0x14:
    data = fdata(data,"netbios data ack [P3] xmitcorrelator [w][P2] remsessnum [B] lclsessnum [B] ",data2);
    break;

  case 0x18:
    data = fdata(data,"end session [P1] data2 [w][P4] remsessnum [B] lclsessnum [B] ",data2);
    break;

  case 0x1f:
    data = fdata(data,"session alive ",data2);
    break;

  default:
    data = fdata(data,"unknown netbios command ",data2);
    break;
  }
  if (data == NULL)
    return;

  if (is_truncated) {
    /* data2 was past the end of the buffer */
    return;
  }

  if (memcmp(data2,"\377SMB",4)==0) {
    print_smb(data2,maxbuf);
  } else {
    int i;
    for (i=0;i<128;i++) {
      if (&data2[i] >= maxbuf)
        break;
      if (memcmp(&data2[i],"\377SMB",4)==0) {
	printf("smb @ %d", i);
	print_smb(&data2[i],maxbuf);
	break;
      }
    }
  }
}


/*
   print IPX-Netbios frames
*/
void ipx_netbios_print(const uchar *data, const uchar *maxbuf)
{
  /* this is a hack till I work out how to parse the rest of the IPX stuff */
  int i;
  startbuf = data;
  for (i=0;i<128;i++)
    if (memcmp(&data[i],"\377SMB",4)==0) {
      fdata(data,"IPX ",&data[i]);
      print_smb(&data[i],maxbuf);
      fflush(stdout);
      break;
    }
  if (i==128)
    fdata(data,"unknown IPX ",maxbuf);
}
