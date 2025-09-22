/*	$OpenBSD: hack.end.c,v 1.17 2017/04/08 22:59:09 gsoares Exp $	*/

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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "hack.h"

#define newttentry() (struct toptenentry *) alloc(sizeof(struct toptenentry))
#define	NAMSZ	8
#define	DTHSZ	40
#define	PERSMAX	1
#define	POINTSMIN	1	/* must be > 0 */
#define	ENTRYMAX	100	/* must be >= 10 */
#define	PERS_IS_UID		/* delete for PERSMAX per name; now per uid */

extern char plname[], pl_character[];

xchar maxdlevel = 1;

struct toptenentry {
	struct toptenentry *tt_next;
	long int points;
	int level,maxlvl,hp,maxhp;
	int uid;
	char plchar;
	char sex;
	char name[NAMSZ+1];
	char death[DTHSZ+1];
	char date[7];		/* yymmdd */
} *tt_head;

static void topten(void);
static void outheader(void);
static int  outentry(int, struct toptenentry *, int);
static char *itoa(int);
static char *ordin(int);

void
done1(int notused)
{
	(void) signal(SIGINT,SIG_IGN);
	pline("Really quit?");
	if(readchar() != 'y') {
		(void) signal(SIGINT,done1);
		clrlin();
		(void) fflush(stdout);
		if(multi > 0) nomul(0);
		return;
	}
	done("quit");
}

int
done2(void)
{
	done1(0);
	return(0);
}

int done_stopprint;
int done_hup;

void
done_intr(int notused)
{
	done_stopprint++;
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);
}

void
done_hangup(int notused)
{
	done_hup++;
	(void) signal(SIGHUP, SIG_IGN);
	done_intr(notused);
}

void
done_in_by(struct monst *mtmp)
{
	static char buf[BUFSZ];

	pline("You die ...");
	if(mtmp->data->mlet == ' '){
		snprintf(buf, sizeof buf, "the ghost of %s", (char *) mtmp->mextra);
		killer = buf;
	} else if(mtmp->mnamelth) {
		snprintf(buf, sizeof buf, "%s called %s",
			mtmp->data->mname, NAME(mtmp));
		killer = buf;
	} else if(mtmp->minvis) {
		snprintf(buf, sizeof buf, "invisible %s", mtmp->data->mname);
		killer = buf;
	} else killer = mtmp->data->mname;
	done("died");
}

/* called with arg "died", "drowned", "escaped", "quit", "choked", "panicked",
   "burned", "starved" or "tricked" */
/* Be careful not to call panic from here! */
void
done(char *st1)
{

#ifdef WIZARD
	if(wizard && *st1 == 'd'){
		u.uswldtim = 0;
		if(u.uhpmax < 0) u.uhpmax = 100;	/* arbitrary */
		u.uhp = u.uhpmax;
		pline("For some reason you are still alive.");
		flags.move = 0;
		if(multi > 0) multi = 0; else multi = -1;
		flags.botl = 1;
		return;
	}
#endif /* WIZARD */
	(void) signal(SIGINT, done_intr);
	(void) signal(SIGQUIT, done_intr);
	(void) signal(SIGHUP, done_hangup);
	if(*st1 == 'q' && u.uhp < 1){
		st1 = "died";
		killer = "quit while already on Charon's boat";
	}
	if(*st1 == 's') killer = "starvation"; else
	if(*st1 == 'd' && st1[1] == 'r') killer = "drowning"; else
	if(*st1 == 'p') killer = "panic"; else
	if(*st1 == 't') killer = "trickery"; else
	if(!strchr("bcd", *st1)) killer = st1;
	paybill();
	clearlocks();
	if(flags.toplin == 1) more();
	if(strchr("bcds", *st1)){
#ifdef WIZARD
	    if(!wizard)
#endif /* WIZARD */
		savebones();
		if(!flags.notombstone)
			outrip();
	}
	if(*st1 == 'c') killer = st1;		/* after outrip() */
	settty(NULL);	/* does a clr_screen() */
	if(!done_stopprint)
		printf("Goodbye %s %s...\n\n", pl_character, plname);
	{ long int tmp;
	  tmp = u.ugold - u.ugold0;
	  if(tmp < 0)
		tmp = 0;
	  if(*st1 == 'd' || *st1 == 'b')
		tmp -= tmp/10;
	  u.urexp += tmp;
	  u.urexp += 50 * maxdlevel;
	  if(maxdlevel > 20)
		u.urexp += 1000*((maxdlevel > 30) ? 10 : maxdlevel - 20);
	}
	if(*st1 == 'e') {
		extern struct monst *mydogs;
		struct monst *mtmp;
		struct obj *otmp;
		int i;
		unsigned worthlessct = 0;
		boolean has_amulet = FALSE;

		killer = st1;
		keepdogs();
		mtmp = mydogs;
		if(mtmp) {
			if(!done_stopprint) printf("You");
			while(mtmp) {
				if(!done_stopprint)
					printf(" and %s", monnam(mtmp));
				if(mtmp->mtame)
					u.urexp += mtmp->mhp;
				mtmp = mtmp->nmon;
			}
			if(!done_stopprint)
		    printf("\nescaped from the dungeon with %ld points,\n",
			u.urexp);
		} else
		if(!done_stopprint)
		  printf("You escaped from the dungeon with %ld points,\n",
		    u.urexp);
		for(otmp = invent; otmp; otmp = otmp->nobj) {
			if(otmp->olet == GEM_SYM){
				objects[otmp->otyp].oc_name_known = 1;
				i = otmp->quan*objects[otmp->otyp].g_val;
				if(i == 0) {
					worthlessct += otmp->quan;
					continue;
				}
				u.urexp += i;
				if(!done_stopprint)
				  printf("\t%s (worth %d Zorkmids),\n",
				    doname(otmp), i);
			} else if(otmp->olet == AMULET_SYM) {
				otmp->known = 1;
				i = (otmp->spe < 0) ? 2 : 5000;
				u.urexp += i;
				if(!done_stopprint)
				  printf("\t%s (worth %d Zorkmids),\n",
				    doname(otmp), i);
				if(otmp->spe >= 0) {
					has_amulet = TRUE;
					killer = "escaped (with amulet)";
				}
			}
		}
		if(worthlessct) if(!done_stopprint)
		  printf("\t%u worthless piece%s of coloured glass,\n",
		  worthlessct, plur(worthlessct));
		if(has_amulet) u.urexp *= 2;
	} else
		if(!done_stopprint)
		  printf("You %s on dungeon level %d with %ld points,\n",
		    st1, dlevel, u.urexp);
	if(!done_stopprint)
	  printf("and %ld piece%s of gold, after %ld move%s.\n",
	    u.ugold, plur(u.ugold), moves, plur(moves));
	if(!done_stopprint)
  printf("You were level %u with a maximum of %d hit points when you %s.\n",
	    u.ulevel, u.uhpmax, st1);
	if(*st1 == 'e' && !done_stopprint){
		getret();	/* all those pieces of coloured glass ... */
		cls();
	}
#ifdef WIZARD
	if(!wizard)
#endif /* WIZARD */
		topten();
	if(done_stopprint) printf("\n\n");
	exit(0);
}

static void
topten(void)
{
	int uid = getuid();
	int rank, rank0 = -1, rank1 = 0;
	int occ_cnt = PERSMAX;
	struct toptenentry *t0, *t1, *tprev;
	char *recfile = RECORD;
	char *reclock = "record_lock";
	int sleepct = 300;
	FILE *rfile;
	int flg = 0;
#define	HUP	if(!done_hup)
	while(link(recfile, reclock) == -1) {
		HUP perror(reclock);
		if(!sleepct--) {
			HUP puts("I give up. Sorry.");
			HUP puts("Perhaps there is an old record_lock around?");
			return;
		}
		HUP printf("Waiting for access to record file. (%d)\n",
			sleepct);
		HUP (void) fflush(stdout);
		sleep(1);
	}
	if(!(rfile = fopen(recfile,"r"))){
		HUP puts("Cannot open record file!");
		goto unlock;
	}
	HUP (void) putchar('\n');

	/* create a new 'topten' entry */
	t0 = newttentry();
	t0->level = dlevel;
	t0->maxlvl = maxdlevel;
	t0->hp = u.uhp;
	t0->maxhp = u.uhpmax;
	t0->points = u.urexp;
	t0->plchar = pl_character[0];
	t0->sex = (flags.female ? 'F' : 'M');
	t0->uid = uid;
	(void) strlcpy(t0->name, plname, sizeof t0->name);
	(void) strlcpy(t0->death, killer, sizeof t0->death);
	(void) strlcpy(t0->date, getdate(), sizeof t0->date);

	/* assure minimum number of points */
	if(t0->points < POINTSMIN)
		t0->points = 0;

	t1 = tt_head = newttentry();
	tprev = 0;
	/* rank0: -1 undefined, 0 not_on_list, n n_th on list */
	for(rank = 1; ; ) {
	  if(fscanf(rfile, "%6s %d %d %d %d %d %ld %c%c %8[^,],%40[^\n]",
		t1->date, &t1->uid,
		&t1->level, &t1->maxlvl,
		&t1->hp, &t1->maxhp, &t1->points,
		&t1->plchar, &t1->sex, t1->name, t1->death) != 11
	  || t1->points < POINTSMIN)
			t1->points = 0;
	  if(rank0 < 0 && t1->points < t0->points) {
		rank0 = rank++;
		if(tprev == 0)
			tt_head = t0;
		else
			tprev->tt_next = t0;
		t0->tt_next = t1;
		occ_cnt--;
		flg++;		/* ask for a rewrite */
	  } else
		tprev = t1;
	  if(t1->points == 0) break;
	  if(
#ifdef PERS_IS_UID
	     t1->uid == t0->uid &&
#else
	     strncmp(t1->name, t0->name, NAMSZ) == 0 &&
#endif /* PERS_IS_UID */
	     t1->plchar == t0->plchar && --occ_cnt <= 0){
		if(rank0 < 0){
			rank0 = 0;
			rank1 = rank;
	HUP printf("You didn't beat your previous score of %ld points.\n\n",
				t1->points);
		}
		if(occ_cnt < 0){
			flg++;
			continue;
		}
	  }
	  if(rank <= ENTRYMAX){
	  	t1 = t1->tt_next = newttentry();
	  	rank++;
	  }
	  if(rank > ENTRYMAX){
		t1->points = 0;
		break;
	  }
	}
	if(flg) {	/* rewrite record file */
		(void) fclose(rfile);
		if(!(rfile = fopen(recfile,"w"))){
			HUP puts("Cannot write record file\n");
			goto unlock;
		}

		if(!done_stopprint) if(rank0 > 0){
		    if(rank0 <= 10)
			puts("You made the top ten list!\n");
		    else
		printf("You reached the %d%s place on the top %d list.\n\n",
			rank0, ordin(rank0), ENTRYMAX);
		}
	}
	if(rank0 == 0) rank0 = rank1;
	if(rank0 <= 0) rank0 = rank;
	if(!done_stopprint) outheader();
	t1 = tt_head;
	for(rank = 1; t1->points != 0; rank++, t1 = t1->tt_next) {
	  if(flg) fprintf(rfile,"%6s %d %d %d %d %d %ld %c%c %s,%s\n",
	    t1->date, t1->uid,
	    t1->level, t1->maxlvl,
	    t1->hp, t1->maxhp, t1->points,
	    t1->plchar, t1->sex, t1->name, t1->death);
	  if(done_stopprint) continue;
	  if(rank > flags.end_top &&
	    (rank < rank0-flags.end_around || rank > rank0+flags.end_around)
	    && (!flags.end_own ||
#ifdef PERS_IS_UID
				  t1->uid != t0->uid ))
#else
				  strncmp(t1->name, t0->name, NAMSZ)))
#endif /* PERS_IS_UID */
	  	continue;
	  if(rank == rank0-flags.end_around &&
	     rank0 > flags.end_top+flags.end_around+1 &&
	     !flags.end_own)
		(void) putchar('\n');
	  if(rank != rank0)
		(void) outentry(rank, t1, 0);
	  else if(!rank1)
		(void) outentry(rank, t1, 1);
	  else {
		int t0lth = outentry(0, t0, -1);
		int t1lth = outentry(rank, t1, t0lth);
		if(t1lth > t0lth) t0lth = t1lth;
		(void) outentry(0, t0, t0lth);
	  }
	}
	if(rank0 >= rank) if(!done_stopprint)
		(void) outentry(0, t0, 1);
	(void) fclose(rfile);
unlock:
	(void) unlink(reclock);
}

static void
outheader(void)
{
	char linebuf[BUFSZ];
	char *bp;

	(void) strlcpy(linebuf, "Number Points  Name", sizeof linebuf);
	bp = eos(linebuf);
	while(bp < linebuf + COLNO - 9) *bp++ = ' ';
	(void) strlcpy(bp, "Hp [max]", linebuf + sizeof linebuf - bp);
	puts(linebuf);
}

/* so>0: standout line; so=0: ordinary line; so<0: no output, return lth */
static int
outentry(int rank, struct toptenentry *t1, int so)
{
	boolean quit = FALSE, killed = FALSE, starv = FALSE;
	char linebuf[BUFSZ];
	char *bp;

	linebuf[0] = 0;
	if(rank)
		snprintf(linebuf, sizeof linebuf, "%3d", rank);
	else
		snprintf(linebuf, sizeof linebuf, "   ");
	bp = eos(linebuf);
	snprintf(bp, linebuf + sizeof linebuf - bp, " %6ld %8s",
	    t1->points, t1->name);
	if(t1->plchar == 'X')
		strlcat(linebuf, " ", sizeof linebuf);
	else {
		bp = eos(linebuf);
		snprintf(bp, linebuf + sizeof linebuf - bp,
		    "-%c ", t1->plchar);
	}
	bp = eos(linebuf);
	if(!strncmp("escaped", t1->death, 7)) {
	  if(!strcmp(" (with amulet)", t1->death+7))
	    snprintf(bp, linebuf + sizeof linebuf - bp,
	     "escaped the dungeon with amulet");
	  else
	    snprintf(bp, linebuf + sizeof linebuf - bp,
	     "escaped the dungeon [max level %d]", t1->maxlvl);
	} else {
	  if(!strncmp(t1->death,"quit",4)) {
	    quit = TRUE;
	    if(t1->maxhp < 3*t1->hp && t1->maxlvl < 4)
	  	strlcat(linebuf, "cravenly gave up", sizeof linebuf);
	    else
		strlcat(linebuf, "quit", sizeof linebuf);
	  }
	  else if(!strcmp(t1->death,"choked"))
	    snprintf(bp, linebuf + sizeof linebuf - bp, "choked on %s food",
		(t1->sex == 'F') ? "her" : "his");
	  else if(!strncmp(t1->death,"starv",5)) {
	    strlcat(linebuf, "starved to death", sizeof linebuf);
	    starv = TRUE;
	  } else {
	    strlcat(linebuf, "was killed", sizeof linebuf);
	    killed = TRUE;
	  }
	  bp = eos(linebuf);
	  snprintf(bp, linebuf + sizeof linebuf - bp, " on%s level %d",
	    (killed || starv) ? "" : " dungeon", t1->level);
	  if(t1->maxlvl != t1->level) {
	    bp = eos(linebuf);
	    snprintf(bp, linebuf + sizeof linebuf - bp,
	     " [max %d]", t1->maxlvl);
	  }
	  if(quit && t1->death[4])
	    strlcat(linebuf, t1->death + 4, sizeof linebuf);
	}
	if(killed) {
	  bp = eos(linebuf);
	  snprintf(bp, linebuf + sizeof linebuf - bp, " by %s%s",
	   (!strncmp(t1->death, "trick", 5) || !strncmp(t1->death, "the ", 4))
		? "" :
	   strchr(vowels,*t1->death) ? "an " : "a ", t1->death);
	}
	strlcat(linebuf, ".", sizeof linebuf);
	if(t1->maxhp) {
	  char hpbuf[10];
	  int hppos;
	  bp = eos(linebuf);
	  snprintf(hpbuf, sizeof hpbuf, "%s", (t1->hp > 0) ? itoa(t1->hp) : "-");
	  hppos = COLNO - 7 - strlen(hpbuf);
	  if(bp <= linebuf + hppos) {
	    while(bp < linebuf + hppos) *bp++ = ' ';
	    (void) strlcpy(bp, hpbuf, linebuf + sizeof linebuf - bp);
	    bp = eos(linebuf);
	    snprintf(bp, linebuf + sizeof linebuf - bp, " [%d]", t1->maxhp);
	  }
	}
	if(so == 0) puts(linebuf);
	else if(so > 0) {
	  bp = eos(linebuf);
	  if(so >= COLNO) so = COLNO-1;
	  while(bp < linebuf + so) *bp++ = ' ';
	  *bp = 0;
	  standoutbeg();
	  fputs(linebuf,stdout);
	  standoutend();
	  (void) putchar('\n');
	}
	return(strlen(linebuf));
}

static char *
itoa(int a)
{
	static char buf[12];

	snprintf(buf, sizeof buf, "%d", a);
	return(buf);
}

static char *
ordin(int n)
{
	int d = n%10;

	return((d==0 || d>3 || n/10==1) ? "th" : (d==1) ? "st" :
		(d==2) ? "nd" : "rd");
}

void
clearlocks(void)
{
	int x;

	(void) signal(SIGHUP,SIG_IGN);
	for(x = maxdlevel; x >= 0; x--) {
		glo(x);
		(void) unlink(lock);	/* not all levels need be present */
	}
}

char *
eos(char *s)
{
	while(*s) s++;
	return(s);
}

/* it is the callers responsibility to check that there is room for c */
void
charcat(char *s, char c)
{
	while(*s) s++;
	*s++ = c;
	*s = 0;
}

/*
 * Called with args from main if argc >= 0. In this case, list scores as
 * requested. Otherwise, find scores for the current player (and list them
 * if argc == -1).
 */
void
prscore(int argc, char **argv)
{
	extern char *__progname;
	char **players;
	int playerct;
	int rank;
	struct toptenentry *t1, *t2;
	char *recfile = RECORD;
	FILE *rfile;
	int flg = 0;
	int i;
#ifdef nonsense
	long total_score = 0L;
	char totchars[10];
	int totcharct = 0;
#endif /* nonsense */
	int outflg = (argc >= -1);
#ifdef PERS_IS_UID
	int uid = -1;
#else
	char *player0;
#endif /* PERS_IS_UID */

	if(!(rfile = fopen(recfile,"r"))){
		puts("Cannot open record file!");
		return;
	}

	if(argc > 1 && !strncmp(argv[1], "-s", 2)){
		if(!argv[1][2]){
			argc--;
			argv++;
		} else if(!argv[1][3] && strchr("CFKSTWX", argv[1][2])) {
			argv[1]++;
			argv[1][0] = '-';
		} else	argv[1] += 2;
	}
	if(argc <= 1){
#ifdef PERS_IS_UID
		uid = getuid();
		playerct = 0;
#else
		player0 = plname;
		if(!*player0)
			player0 = "hackplayer";
		playerct = 1;
		players = &player0;
#endif /* PERS_IS_UID */
	} else {
		playerct = --argc;
		players = ++argv;
	}
	if(outflg) putchar('\n');

	t1 = tt_head = newttentry();
	for(rank = 1; ; rank++) {
	  if(fscanf(rfile, "%6s %d %d %d %d %d %ld %c%c %8[^,],%40[^\n]",
		t1->date, &t1->uid,
		&t1->level, &t1->maxlvl,
		&t1->hp, &t1->maxhp, &t1->points,
		&t1->plchar, &t1->sex, t1->name, t1->death) != 11)
			t1->points = 0;
	  if(t1->points == 0) break;
#ifdef PERS_IS_UID
	  if(!playerct && t1->uid == uid)
		flg++;
	  else
#endif /* PERS_IS_UID */
	  for(i = 0; i < playerct; i++){
		if(strcmp(players[i], "all") == 0 ||
		   strncmp(t1->name, players[i], NAMSZ) == 0 ||
		  (players[i][0] == '-' &&
		   players[i][1] == t1->plchar &&
		   players[i][2] == 0) ||
		  (isdigit((unsigned char)players[i][0]) && rank <= atoi(players[i])))
			flg++;
	  }
	  t1 = t1->tt_next = newttentry();
	}
	(void) fclose(rfile);
	if(!flg) {
	    if(outflg) {
		printf("Cannot find any entries for ");
		if(playerct < 1) printf("you.\n");
		else {
		  if(playerct > 1) printf("any of ");
		  for(i=0; i<playerct; i++)
			printf("%s%s", players[i], (i<playerct-1)?", ":".\n");
		  printf("Call is: %s -s [playernames]\n", __progname);
		}
	    }
	    return;
	}

	if(outflg) outheader();
	t1 = tt_head;
	for(rank = 1; t1->points != 0; rank++, t1 = t2) {
		t2 = t1->tt_next;
#ifdef PERS_IS_UID
		if(!playerct && t1->uid == uid)
			goto outwithit;
		else
#endif /* PERS_IS_UID */
		for(i = 0; i < playerct; i++){
			if(strcmp(players[i], "all") == 0 ||
			   strncmp(t1->name, players[i], NAMSZ) == 0 ||
			  (players[i][0] == '-' &&
			   players[i][1] == t1->plchar &&
			   players[i][2] == 0) ||
			  (isdigit((unsigned char)players[i][0]) && rank <= atoi(players[i]))){
			outwithit:
				if(outflg)
				    (void) outentry(rank, t1, 0);
#ifdef nonsense
				total_score += t1->points;
				if(totcharct < sizeof(totchars)-1)
				    totchars[totcharct++] = t1->plchar;
#endif /* nonsense */
				break;
			}
		}
		free(t1);
	}
#ifdef nonsense
	totchars[totcharct] = 0;

	/* We would like to determine whether he is experienced. However,
	   the information collected here only tells about the scores/roles
	   that got into the topten (top 100?). We should maintain a
	   .hacklog or something in his home directory. */
	flags.beginner = (total_score < 6000);
	for(i=0; i<6; i++)
	    if(!strchr(totchars, "CFKSTWX"[i])) {
		flags.beginner = 1;
		if(!pl_character[0]) pl_character[0] = "CFKSTWX"[i];
		break;
	}
#endif /* nonsense */
}
