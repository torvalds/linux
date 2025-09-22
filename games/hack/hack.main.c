/*	$OpenBSD: hack.main.c,v 1.26 2023/09/06 11:53:56 jsg Exp $	*/

/*
 * Copyright (c) 1985, Stichting Centrum voor Wiskunde en Informatica,
 * Amsterdam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Stichting Centrum voor Wiskunde en
 * Informatica, nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982 Jay Fenlason <hack@gnu.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "hack.h"

#ifdef QUEST
#define	gamename	"quest"
#else
#define	gamename	"hack"
#endif

extern char plname[PL_NSIZ], pl_character[PL_CSIZ];
extern struct permonst mons[CMNUM+2];
extern char genocided[60], fut_geno[60];

void (*afternmv)(void);
int (*occupation)(void);
char *occtxt;			/* defined when occupation != NULL */

int hackpid;				/* current pid */
int locknum;				/* max num of players */
#ifdef DEF_PAGER
char *catmore;				/* default pager */
#endif
char SAVEF[PL_NSIZ + 11] = "save/";	/* save/99999player */
char obuf[BUFSIZ];	/* BUFSIZ is defined in stdio.h */

extern char *nomovemsg;
extern long wailmsg;

#ifdef CHDIR
static void chdirx(char *, boolean);
#endif

int
main(int argc, char **argv)
{
	int fd;
#ifdef CHDIR
	char *dir;
#endif

	hackpid = getpid();

#ifdef CHDIR			/* otherwise no chdir() */
	/*
	 * See if we must change directory to the playground.
	 * (Perhaps hack runs suid and playground is inaccessible
	 *  for the player.)
	 * The environment variable HACKDIR is overridden by a
	 *  -d command line option (must be the first option given)
	 */

	dir = getenv("HACKDIR");
	if(argc > 1 && !strncmp(argv[1], "-d", 2)) {
		argc--;
		argv++;
		dir = argv[0]+2;
		if(*dir == '=' || *dir == ':') dir++;
		if(!*dir && argc > 1) {
			argc--;
			argv++;
			dir = argv[0];
		}
		if(!*dir)
		    error("Flag -d must be followed by a directory name.");
	}
#endif

	/*
	 * Who am i? Algorithm: 1. Use name as specified in HACKOPTIONS
	 *			2. Use $LOGNAME or $USER	(if 1. fails)
	 *			3. Use getlogin()		(if 2. fails)
	 * The resulting name is overridden by command line options.
	 * If everything fails, or if the resulting name is some generic
	 * account like "games", "play", "player", "hack" then eventually
	 * we'll ask him.
	 * Note that we trust him here; it is possible to play under
	 * somebody else's name.
	 */
	{ char *s;

	  initoptions();
	  if(!*plname && (s = getenv("LOGNAME")))
		(void) strlcpy(plname, s, sizeof(plname));
	  if(!*plname && (s = getenv("USER")))
		(void) strlcpy(plname, s, sizeof(plname));
	  if(!*plname && (s = getlogin()))
		(void) strlcpy(plname, s, sizeof(plname));
	}

	/*
	 * Now we know the directory containing 'record' and
	 * may do a prscore().
	 */
	if(argc > 1 && !strncmp(argv[1], "-s", 2)) {
#ifdef CHDIR
		chdirx(dir,0);
#endif
		prscore(argc, argv);
		return 0;
	}

	/*
	 * It seems he really wants to play.
	 * Remember tty modes, to be restored on exit.
	 */
	gettty();
	setvbuf(stdout, obuf, _IOFBF, sizeof obuf);
	umask(007);
	startup();
	cls();
	u.uhp = 1;	/* prevent RIP on early quits */
	u.ux = FAR;	/* prevent nscr() */
	(void) signal(SIGHUP, hackhangup);

#ifdef CHDIR
	chdirx(dir,1);
#endif

	/*
	 * Process options.
	 */
	while(argc > 1 && argv[1][0] == '-'){
		argv++;
		argc--;
		switch(argv[0][1]){
#ifdef WIZARD
		case 'D':
/*			if(!strcmp(getlogin(), WIZARD)) */
				wizard = TRUE;
/*			else
				printf("Sorry.\n"); */
			break;
#endif
#ifdef NEWS
		case 'n':
			flags.nonews = TRUE;
			break;
#endif
		case 'u':
			if(argv[0][2]) {
			  (void) strlcpy(plname, argv[0]+2, sizeof(plname));
			} else if(argc > 1) {
			  argc--;
			  argv++;
			  (void) strlcpy(plname, argv[0], sizeof(plname));
			} else
				printf("Player name expected after -u\n");
			break;
		default:
			/* allow -T for Tourist, etc. */
			(void) strlcpy(pl_character, argv[0]+1, sizeof(pl_character));
			/* printf("Unknown option: %s\n", *argv); */
		}
	}

	if(argc > 1)
		locknum = atoi(argv[1]);
#ifdef MAX_NR_OF_PLAYERS
	if(!locknum || locknum > MAX_NR_OF_PLAYERS)
		locknum = MAX_NR_OF_PLAYERS;
#endif
#ifdef DEF_PAGER
	if(!(catmore = getenv("HACKPAGER")) && !(catmore = getenv("PAGER")))
		catmore = DEF_PAGER;
#endif
#ifdef MAIL
	getmailstatus();
#endif
#ifdef WIZARD
	if(wizard) (void) strlcpy(plname, "wizard", sizeof plname); else
#endif
	if(!*plname || !strncmp(plname, "player", 4)
		    || !strncmp(plname, "games", 4))
		askname();
	plnamesuffix();		/* strip suffix from name; calls askname() */
				/* again if suffix was whole name */
				/* accepts any suffix */
#ifdef WIZARD
	if(!wizard) {
#endif
		/*
		 * check for multiple games under the same name
		 * (if !locknum) or check max nr of players (otherwise)
		 */
		(void) signal(SIGQUIT,SIG_IGN);
		(void) signal(SIGINT,SIG_IGN);
		if(!locknum)
			(void) strlcpy(lock,plname,sizeof lock);
		getlock();	/* sets lock if locknum != 0 */
#ifdef WIZARD
	} else {
		char *sfoo;
		(void) strlcpy(lock,plname,sizeof lock);
		if ((sfoo = getenv("MAGIC")))
			while(*sfoo) {
				switch(*sfoo++) {
				case 'n': (void) srandom_deterministic(*sfoo++);
					break;
				}
			}
		if ((sfoo = getenv("GENOCIDED"))) {
			if(*sfoo == '!'){
				struct permonst *pm = mons;
				char *gp = genocided;

				while(pm < mons+CMNUM+2){
					if(!strchr(sfoo, pm->mlet))
						*gp++ = pm->mlet;
					pm++;
				}
				*gp = 0;
			} else
				strlcpy(genocided, sfoo, sizeof genocided);
			strlcpy(fut_geno, genocided, sizeof fut_geno);
		}
	}
#endif
	setftty();
	(void) snprintf(SAVEF, sizeof SAVEF, "save/%u%s", getuid(), plname);
	regularize(SAVEF+5);		/* avoid . or / in name */
	if((fd = open(SAVEF, O_RDONLY)) >= 0) {
		(void) signal(SIGINT,done1);
		pline("Restoring old save file...");
		(void) fflush(stdout);
		if(!dorecover(fd))
			goto not_recovered;
		pline("Hello %s, welcome to %s!", plname, gamename);
		flags.move = 0;
	} else {
not_recovered:
		fobj = fcobj = invent = 0;
		fmon = fallen_down = 0;
		ftrap = 0;
		fgold = 0;
		flags.ident = 1;
		init_objects();
		u_init();

		(void) signal(SIGINT,done1);
		mklev();
		u.ux = xupstair;
		u.uy = yupstair;
		(void) inshop();
		setsee();
		flags.botlx = 1;
		makedog();
		{ struct monst *mtmp;
		  if ((mtmp = m_at(u.ux, u.uy)))
			  mnexto(mtmp);	/* riv05!a3 */
		}
		seemons();
#ifdef NEWS
		if(flags.nonews || !readnews())
			/* after reading news we did docrt() already */
#endif
			docrt();

		/* give welcome message before pickup messages */
		pline("Hello %s, welcome to %s!", plname, gamename);

		pickup(1);
		read_engr_at(u.ux,u.uy);
		flags.move = 1;
	}

	flags.moonphase = phase_of_the_moon();
	if(flags.moonphase == FULL_MOON) {
		pline("You are lucky! Full moon tonight.");
		u.uluck++;
	} else if(flags.moonphase == NEW_MOON) {
		pline("Be careful! New moon tonight.");
	}

	initrack();

	for(;;) {
		if(flags.move) {	/* actual time passed */

			settrack();

			if(moves%2 == 0 ||
			  (!(Fast & ~INTRINSIC) && (!Fast || rn2(3)))) {
				movemon();
				if(!rn2(70))
				    (void) makemon((struct permonst *)0, 0, 0);
			}
			if(Glib) glibr();
			hacktimeout();
			++moves;
			if(flags.time) flags.botl = 1;
			if(u.uhp < 1) {
				pline("You die...");
				done("died");
			}
			if(u.uhp*10 < u.uhpmax && moves-wailmsg > 50){
			    wailmsg = moves;
			    if(u.uhp == 1)
			    pline("You hear the wailing of the Banshee...");
			    else
			    pline("You hear the howling of the CwnAnnwn...");
			}
			if(u.uhp < u.uhpmax) {
				if(u.ulevel > 9) {
					if(Regeneration || !(moves%3)) {
					    flags.botl = 1;
					    u.uhp += rnd((int) u.ulevel-9);
					    if(u.uhp > u.uhpmax)
						u.uhp = u.uhpmax;
					}
				} else if(Regeneration ||
					(!(moves%(22-u.ulevel*2)))) {
					flags.botl = 1;
					u.uhp++;
				}
			}
			if(Teleportation && !rn2(85)) tele();
			if(Searching && multi >= 0) (void) dosearch();
			gethungry();
			invault();
			amulet();
		}
		if(multi < 0) {
			if(!++multi){
				pline("%s", nomovemsg ? nomovemsg :
					"You can move again.");
				nomovemsg = 0;
				if(afternmv) (*afternmv)();
				afternmv = 0;
			}
		}

		find_ac();
#ifndef QUEST
		if(!flags.mv || Blind)
#endif
		{
			seeobjs();
			seemons();
			nscr();
		}
		if(flags.botl || flags.botlx) bot();

		flags.move = 1;

		if(multi >= 0 && occupation) {
			if(monster_nearby())
				stop_occupation();
			else if ((*occupation)() == 0)
				occupation = 0;
			continue;
		}

		if(multi > 0) {
#ifdef QUEST
			if(flags.run >= 4) finddir();
#endif
			lookaround();
			if(!multi) {	/* lookaround may clear multi */
				flags.move = 0;
				continue;
			}
			if(flags.mv) {
				if(multi < COLNO && !--multi)
					flags.mv = flags.run = 0;
				domove();
			} else {
				--multi;
				rhack(save_cm);
			}
		} else if(multi == 0) {
#ifdef MAIL
			ckmailstatus();
#endif
			rhack(NULL);
		}
		if(multi && multi%7 == 0)
			(void) fflush(stdout);
	}
}

void
glo(int foo)
{
	/* construct the string  xlock.n  */
	char *tf;

	tf = lock;
	while(*tf && *tf != '.') tf++;
	(void) snprintf(tf, lock + sizeof lock - tf, ".%d", foo);
}

/*
 * plname is filled either by an option (-u Player  or  -uPlayer) or
 * explicitly (-w implies wizard) or by askname.
 * It may still contain a suffix denoting pl_character.
 */
void
askname(void)
{
	int c,ct;

	printf("\nWho are you? ");
	(void) fflush(stdout);
	ct = 0;
	while((c = getchar()) != '\n'){
		if(c == EOF) error("End of input\n");
		/* some people get confused when their erase char is not ^H */
		if(c == '\010') {
			if(ct) ct--;
			continue;
		}
		if(c != '-')
		if(c < 'A' || (c > 'Z' && c < 'a') || c > 'z') c = '_';
		if(ct < sizeof(plname)-1) plname[ct++] = c;
	}
	plname[ct] = 0;
	if(ct == 0) askname();
}

void
impossible(const char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	vpline(s, ap);
	va_end(ap);
	pline("Program in disorder - perhaps you'd better Quit.");
}

#ifdef CHDIR
static void
chdirx(char *dir, boolean wr)
{
	gid_t gid;

#ifdef SECURE
	if(dir					/* User specified directory? */
#ifdef HACKDIR
	       && strcmp(dir, HACKDIR)		/* and not the default? */
#endif
		) {
		/* revoke privs */
		gid = getgid();
		setresgid(gid, gid, gid);
	}
#endif

#ifdef HACKDIR
	if(dir == NULL)
		dir = HACKDIR;
#endif

	if(dir && chdir(dir) == -1) {
		perror(dir);
		error("Cannot chdir to %s.", dir);
	}

	/* warn the player if he cannot write the record file */
	/* warn the player if he cannot read the permanent lock file */
	/* warn the player if he cannot create the save directory */
	/* perhaps we should also test whether . is writable */
	/* unfortunately the access systemcall is worthless */
	if(wr) {
	    int fd;

	    if(dir == NULL)
		dir = ".";
	    if((fd = open(RECORD, O_RDWR | O_CREAT, FMASK)) == -1) {
		printf("Warning: cannot write %s/%s", dir, RECORD);
		getret();
	    } else
		(void) close(fd);
	    if((fd = open(HLOCK, O_RDONLY | O_CREAT, FMASK)) == -1) {
		printf("Warning: cannot read %s/%s", dir, HLOCK);
		getret();
	    } else
		(void) close(fd);
	    if(mkdir("save", 0770) && errno != EEXIST) {
		printf("Warning: cannot create %s/save", dir);
		getret();
	    }
	}
}
#endif

void
stop_occupation(void)
{
	if(occupation) {
		pline("You stop %s.", occtxt);
		occupation = 0;
	}
}
