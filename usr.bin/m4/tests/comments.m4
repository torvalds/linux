dnl $FreeBSD$
dnl $OpenBSD: src/regress/usr.bin/m4/comments.m4,v 1.1 2005/09/06 15:33:21 espie Exp $
dnl checking the way changecom works.
1: normal
define(`comment', `COMMENT')dnl
define(`p', 'XXX')dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

2: `changecom(>>)dnl'
changecom(>>)dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

3: `changecom dnl'
changecom dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

4: `changecom()dnl'
changecom()dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

5: `changecom(,)dnl'
changecom(,)dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

6: `changecom(`p',q)dnl'
changecom(`p',q)dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

7: `changecom(`p')dnl'
changecom(`p')dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too

8: `changecom(#)dnl'
changecom(#)dnl
# this is a comment
>> this is a comment
p this is a comment
p this is a comment q comment too
