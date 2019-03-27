\ Copyright (c) 2008-2011 Devin Teske <dteske@FreeBSD.org>
\ All rights reserved.
\ 
\ Redistribution and use in source and binary forms, with or without
\ modification, are permitted provided that the following conditions
\ are met:
\ 1. Redistributions of source code must retain the above copyright
\    notice, this list of conditions and the following disclaimer.
\ 2. Redistributions in binary form must reproduce the above copyright
\    notice, this list of conditions and the following disclaimer in the
\    documentation and/or other materials provided with the distribution.
\ 
\ THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
\ ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
\ IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
\ ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
\ FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
\ DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
\ OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
\ HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
\ LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
\ OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
\ SUCH DAMAGE.
\ 
\ $FreeBSD$

\ FICL words intended to be used as shortcuts for carrying out common tasks or
\ producing common results. Generally, words defined here are simply groupings
\ of other custom words that pull from multiple libraries (for example, if you
\ want to define a custom word that uses words defined in three different
\ libraries, this is a good place to define such a word).
\ 
\ This script should be included after you have included any/all other
\ libraries. This will prevent calling a word defined here before any required
\ words have been defined.

marker task-shortcuts.4th

\ This "shortcut" word will not be used directly, but is defined here to
\ offer the user a quick way to get back into the interactive PXE menu
\ after they have escaped to the shell (perhaps by accident).
\ 
: menu ( -- )
	clear           \ Clear the screen (in screen.4th)
	print_version   \ print version string (bottom-right; see version.4th)
	draw-beastie    \ Draw FreeBSD logo at right (in beastie.4th)
	draw-brand      \ Draw FIS logo at top (in brand.4th)
	menu-init       \ Initialize menu and draw bounding box (in menu.4th)
	menu-display    \ Launch interactive menu (in menu.4th)
;
