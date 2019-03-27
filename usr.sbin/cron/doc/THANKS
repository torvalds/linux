15 January 1990
Paul Vixie

Many people have contributed to cron.  Many more than I can remember, in fact.
Rich Salz and Carl Gutekunst were each of enormous help to me in V1; Carl for
helping me understand UNIX well enough to write it, and Rich for helping me
get the features right.

John Gilmore wrote me a wonderful review of V2, which took me a whole year to
answer even though it made me clean up some really awful things in the code.
(According to John the most awful things are still in here, of course.)

Paul Close made a suggestion which led to /etc/crond.pid and the mutex locking
on it.  Kevin Braunsdorf of Purdue made a suggestion that led to @reboot and
its brothers and sisters; he also sent some diffs that lead cron toward compil-
ability with System V, though without at(1) capabilities, this cron isn't going
to be that useful on System V.  Bob Alverson fixed a silly bug in the line
number counting.  Brian Reid made suggestions which led to the run queue and
the source-file labelling in installed crontabs.

Scott Narveson ported V2 to a Sequent, and sent in the most useful single batch
of diffs I got from anybody.  Changes attributable to Scott are:
	-> sendmail won't time out if the command is slow to generate output
	-> day-of-week names aren't off by one anymore
	-> crontab says the right thing if you do something you shouldn't do
	-> crontab(5) man page is longer and more informative
	-> misc changes related to the side effects of fclose()
	-> Sequent "universe" support added (may also help on Pyramids)
	-> null pw_shell is dealt with now; default is /bin/sh
