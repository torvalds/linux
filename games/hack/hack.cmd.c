/*	$OpenBSD: hack.cmd.c,v 1.9 2016/01/09 18:33:15 mestre Exp $	*/

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

#include "def.func_tab.h"
#include "hack.h"

struct func_tab cmdlist[]={
	{ '\020', doredotopl },
	{ '\022', doredraw },
	{ '\024', dotele },
#ifdef SUSPEND
	{ '\032', dosuspend },
#endif /* SUSPEND */
	{ 'a', doapply },
/*	'A' : UNUSED */
/*	'b', 'B' : go sw */
	{ 'c', ddocall },
	{ 'C', do_mname },
	{ 'd', dodrop },
	{ 'D', doddrop },
	{ 'e', doeat },
	{ 'E', doengrave },
/*	'f', 'F' : multiple go (might become 'fight') */
/*	'g', 'G' : UNUSED */
/*	'h', 'H' : go west */
	{ 'I', dotypeinv },		/* Robert Viduya */
	{ 'i', ddoinv },
/*	'j', 'J', 'k', 'K', 'l', 'L', 'm', 'M', 'n', 'N' : move commands */
/*	'o', doopen,	*/
	{ 'O', doset },
	{ 'p', dopay },
	{ 'P', dowearring },
	{ 'q', dodrink },
	{ 'Q', done2 },
	{ 'r', doread },
	{ 'R', doremring },
	{ 's', dosearch },
	{ 'S', dosave },
	{ 't', dothrow },
	{ 'T', doremarm },
/*	'u', 'U' : go ne */
	{ 'v', doversion },
/*	'V' : UNUSED */
	{ 'w', dowield },
	{ 'W', doweararm },
/*	'x', 'X' : UNUSED */
/*	'y', 'Y' : go nw */
	{ 'z', dozap },
/*	'Z' : UNUSED */
	{ '<', doup },
	{ '>', dodown },
	{ '/', dowhatis },
	{ '?', dohelp },
#ifdef SHELL
	{ '!', dosh },
#endif /* SHELL */
	{ '.', donull },
	{ ' ', donull },
	{ ',', dopickup },
	{ ':', dolook },
	{ '^', doidtrap },
	{ '\\', dodiscovered },		/* Robert Viduya */
	{ WEAPON_SYM,  doprwep },
	{ ARMOR_SYM,  doprarm },
	{ RING_SYM,  doprring },
	{ '$', doprgold },
	{ '#', doextcmd },
	{ '\0', NULL }
};

struct ext_func_tab extcmdlist[] = {
	{ "dip", dodip },
	{ "pray", dopray },
	{ NULL, donull}
};

extern char quitchars[];

static char unctrl(char);

void
rhack(char *cmd)
{
	struct func_tab *tlist = cmdlist;
	boolean firsttime = FALSE;
	int res;

	if(!cmd) {
		firsttime = TRUE;
		flags.nopick = 0;
		cmd = parse();
	}
	if(!*cmd || (*cmd & 0377) == 0377 ||
	   (flags.no_rest_on_space && *cmd == ' ')){
		hackbell();
		flags.move = 0;
		return;		/* probably we just had an interrupt */
	}
	if(movecmd(*cmd)) {
	walk:
		if(multi) flags.mv = 1;
		domove();
		return;
	}
	if(movecmd(tolower((unsigned char)*cmd))) {
		flags.run = 1;
	rush:
		if(firsttime){
			if(!multi) multi = COLNO;
			u.last_str_turn = 0;
		}
		flags.mv = 1;
#ifdef QUEST
		if(flags.run >= 4) finddir();
		if(firsttime){
			u.ux0 = u.ux + u.dx;
			u.uy0 = u.uy + u.dy;
		}
#endif /* QUEST */
		domove();
		return;
	}
	if((*cmd == 'f' && movecmd(cmd[1])) || movecmd(unctrl(*cmd))) {
		flags.run = 2;
		goto rush;
	}
	if(*cmd == 'F' && movecmd(tolower((unsigned char)cmd[1]))) {
		flags.run = 3;
		goto rush;
	}
	if(*cmd == 'm' && movecmd(cmd[1])) {
		flags.run = 0;
		flags.nopick = 1;
		goto walk;
	}
	if(*cmd == 'M' && movecmd(tolower((unsigned char)cmd[1]))) {
		flags.run = 1;
		flags.nopick = 1;
		goto rush;
	}
#ifdef QUEST
	if(*cmd == cmd[1] && (*cmd == 'f' || *cmd == 'F')) {
		flags.run = 4;
		if(*cmd == 'F') flags.run += 2;
		if(cmd[2] == '-') flags.run += 1;
		goto rush;
	}
#endif /* QUEST */
	while(tlist->f_char) {
		if(*cmd == tlist->f_char){
			res = (*(tlist->f_funct))();
			if(!res) {
				flags.move = 0;
				multi = 0;
			}
			return;
		}
		tlist++;
	}
	{ char expcmd[10];
	  char *cp = expcmd;
	  while(*cmd && cp-expcmd < sizeof(expcmd)-2) {
		if(*cmd >= 040 && *cmd < 0177)
			*cp++ = *cmd++;
		else {
			*cp++ = '^';
			*cp++ = *cmd++ ^ 0100;
		}
	  }
	  *cp++ = 0;
	  pline("Unknown command '%s'.", expcmd);
	}
	multi = flags.move = 0;
}

int
doextcmd(void)	/* here after # - now read a full-word command */
{
	char buf[BUFSZ];
	struct ext_func_tab *efp = extcmdlist;

	pline("# ");
	getlin(buf);
	clrlin();
	if(buf[0] == '\033')
		return(0);
	while(efp->ef_txt) {
		if(!strcmp(efp->ef_txt, buf))
			return((*(efp->ef_funct))());
		efp++;
	}
	pline("%s: unknown command.", buf);
	return(0);
}

static char
unctrl(char sym)
{
    return( (sym >= ('A' & 037) && sym <= ('Z' & 037)) ? sym + 0140 : sym );
}

/* 'rogue'-like direction commands */
char sdir[] = "hykulnjb><";
schar xdir[10] = { -1,-1, 0, 1, 1, 1, 0,-1, 0, 0 };
schar ydir[10] = {  0,-1,-1,-1, 0, 1, 1, 1, 0, 0 };
schar zdir[10] = {  0, 0, 0, 0, 0, 0, 0, 0, 1,-1 };

int
movecmd(char sym)	/* also sets u.dz, but returns false for <> */
{
	char *dp;

	u.dz = 0;
	if(!(dp = strchr(sdir, sym))) return(0);
	u.dx = xdir[dp-sdir];
	u.dy = ydir[dp-sdir];
	u.dz = zdir[dp-sdir];
	return(!u.dz);
}

int
getdir(boolean s)
{
	char dirsym;

	if(s) pline("In what direction?");
	dirsym = readchar();
	if(!movecmd(dirsym) && !u.dz) {
		if(!strchr(quitchars, dirsym))
			pline("What a strange direction!");
		return(0);
	}
	if(Confusion && !u.dz)
		confdir();
	return(1);
}

void
confdir(void)
{
	int x = rn2(8);
	u.dx = xdir[x];
	u.dy = ydir[x];
}

#ifdef QUEST
void
finddir(void)
{
	int i, ui = u.di;

	for(i = 0; i <= 8; i++){
		if(flags.run & 1) ui++; else ui += 7;
		ui %= 8;
		if(i == 8){
			pline("Not near a wall.");
			flags.move = multi = 0;
			return;
		}
		if(!isroom(u.ux+xdir[ui], u.uy+ydir[ui]))
			break;
	}
	for(i = 0; i <= 8; i++){
		if(flags.run & 1) ui += 7; else ui++;
		ui %= 8;
		if(i == 8){
			pline("Not near a room.");
			flags.move = multi = 0;
			return;
		}
		if(isroom(u.ux+xdir[ui], u.uy+ydir[ui]))
			break;
	}
	u.di = ui;
	u.dx = xdir[ui];
	u.dy = ydir[ui];
}

int
isroom(int x, int y)
{		/* what about POOL? */
	return(isok(x,y) && (levl[x][y].typ == ROOM ||
				(levl[x][y].typ >= LDOOR && flags.run >= 6)));
}
#endif /* QUEST */

int
isok(int x, int y)
{
	/* x corresponds to curx, so x==1 is the first column. Ach. %% */
	return(x >= 1 && x <= COLNO-1 && y >= 0 && y <= ROWNO-1);
}
