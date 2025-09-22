/*	$OpenBSD: _def_messages.c,v 1.6 2016/05/23 00:05:15 guenther Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <locale.h>
#include "localedef.h"

const _MessagesLocale _DefaultMessagesLocale =
{
	"^[Yy]",
	"^[Nn]",
	"yes",
	"no"
} ;

const _MessagesLocale *_CurrentMessagesLocale = &_DefaultMessagesLocale;
