/* @(#)dir.x	2.1 88/08/02 4.0 RPCSRC */
/*
 * dir.x: Remote directory listing protocol
 */
const MAXNAMELEN = 255;		/* maximum length of a directory entry */

typedef string nametype<MAXNAMELEN>;	/* a directory entry */

typedef struct namenode *namelist;	/* a link in the listing */

/*
 * A node in the directory listing
 */
struct namenode {
	nametype name;		/* name of directory entry */
	namelist next;		/* next entry */
};

/*
 * The result of a READDIR operation.
 */
union readdir_res switch (int errno) {
case 0:
	namelist list;	/* no error: return directory listing */
default:
	void;		/* error occurred: nothing else to return */
};

/*
 * The directory program definition
 */
program DIRPROG {
	version DIRVERS {
		readdir_res
		READDIR(nametype) = 1;
	} = 1;
} = 76;
