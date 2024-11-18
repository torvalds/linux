/* Sample XDR specification from RFC 1832 Section 5.5 */

const MAXUSERNAME = 32;     /* max length of a user name */
const MAXFILELEN = 65535;   /* max length of a file      */
const MAXNAMELEN = 255;     /* max length of a file name */

/*
 * Types of files:
 */
enum filekind {
   TEXT = 0,       /* ascii data */
   DATA = 1,       /* raw data   */
   EXEC = 2        /* executable */
};

/*
 * File information, per kind of file:
 */
union filetype switch (filekind kind) {
case TEXT:
   void;                           /* no extra information */
case DATA:
   string creator<MAXNAMELEN>;     /* data creator         */
case EXEC:
   string interpretor<MAXNAMELEN>; /* program interpretor  */
};

/*
 * A complete file:
 */
struct file {
   string filename<MAXNAMELEN>; /* name of file    */
   filetype type;               /* info about file */
   string owner<MAXUSERNAME>;   /* owner of file   */
   opaque data<MAXFILELEN>;     /* file data       */
};
