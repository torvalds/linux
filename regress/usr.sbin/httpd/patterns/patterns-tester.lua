-- $OpenBSD: patterns-tester.lua,v 1.1 2015/06/23 18:03:09 semarie Exp $
print(string.format("string='%s'\npattern='%s'", arg[1], arg[2]));
print(string.match(arg[1], arg[2]));
