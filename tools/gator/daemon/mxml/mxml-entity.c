/*
 * "$Id: mxml-entity.c 451 2014-01-04 21:50:06Z msweet $"
 *
 * Character entity support code for Mini-XML, a small XML-like
 * file parsing library.
 *
 * Copyright 2003-2014 by Michael R Sweet.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Michael R Sweet and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at:
 *
 *     http://www.msweet.org/projects.php/Mini-XML
 */

/*
 * Include necessary headers...
 */

#include "mxml-private.h"


/*
 * 'mxmlEntityAddCallback()' - Add a callback to convert entities to Unicode.
 */

int					/* O - 0 on success, -1 on failure */
mxmlEntityAddCallback(
    mxml_entity_cb_t cb)		/* I - Callback function to add */
{
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  if (global->num_entity_cbs < (int)(sizeof(global->entity_cbs) / sizeof(global->entity_cbs[0])))
  {
    global->entity_cbs[global->num_entity_cbs] = cb;
    global->num_entity_cbs ++;

    return (0);
  }
  else
  {
    mxml_error("Unable to add entity callback!");

    return (-1);
  }
}


/*
 * 'mxmlEntityGetName()' - Get the name that corresponds to the character value.
 *
 * If val does not need to be represented by a named entity, NULL is returned.
 */

const char *				/* O - Entity name or NULL */
mxmlEntityGetName(int val)		/* I - Character value */
{
  switch (val)
  {
    case '&' :
        return ("amp");

    case '<' :
        return ("lt");

    case '>' :
        return ("gt");

    case '\"' :
        return ("quot");

    default :
        return (NULL);
  }
}


/*
 * 'mxmlEntityGetValue()' - Get the character corresponding to a named entity.
 *
 * The entity name can also be a numeric constant. -1 is returned if the
 * name is not known.
 */

int					/* O - Character value or -1 on error */
mxmlEntityGetValue(const char *name)	/* I - Entity name */
{
  int		i;			/* Looping var */
  int		ch;			/* Character value */
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  for (i = 0; i < global->num_entity_cbs; i ++)
    if ((ch = (global->entity_cbs[i])(name)) >= 0)
      return (ch);

  return (-1);
}


/*
 * 'mxmlEntityRemoveCallback()' - Remove a callback.
 */

void
mxmlEntityRemoveCallback(
    mxml_entity_cb_t cb)		/* I - Callback function to remove */
{
  int		i;			/* Looping var */
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  for (i = 0; i < global->num_entity_cbs; i ++)
    if (cb == global->entity_cbs[i])
    {
     /*
      * Remove the callback...
      */

      global->num_entity_cbs --;

      if (i < global->num_entity_cbs)
        memmove(global->entity_cbs + i, global->entity_cbs + i + 1,
	        (global->num_entity_cbs - i) * sizeof(global->entity_cbs[0]));

      return;
    }
}


/*
 * '_mxml_entity_cb()' - Lookup standard (X)HTML entities.
 */

int					/* O - Unicode value or -1 */
_mxml_entity_cb(const char *name)	/* I - Entity name */
{
  int	diff,				/* Difference between names */
	current,			/* Current entity in search */
	first,				/* First entity in search */
	last;				/* Last entity in search */
  static const struct
  {
    const char	*name;			/* Entity name */
    int		val;			/* Character value */
  }	entities[] =
  {
    { "AElig",		198 },
    { "Aacute",		193 },
    { "Acirc",		194 },
    { "Agrave",		192 },
    { "Alpha",		913 },
    { "Aring",		197 },
    { "Atilde",		195 },
    { "Auml",		196 },
    { "Beta",		914 },
    { "Ccedil",		199 },
    { "Chi",		935 },
    { "Dagger",		8225 },
    { "Delta",		916 },
    { "Dstrok",		208 },
    { "ETH",		208 },
    { "Eacute",		201 },
    { "Ecirc",		202 },
    { "Egrave",		200 },
    { "Epsilon",	917 },
    { "Eta",		919 },
    { "Euml",		203 },
    { "Gamma",		915 },
    { "Iacute",		205 },
    { "Icirc",		206 },
    { "Igrave",		204 },
    { "Iota",		921 },
    { "Iuml",		207 },
    { "Kappa",		922 },
    { "Lambda",		923 },
    { "Mu",		924 },
    { "Ntilde",		209 },
    { "Nu",		925 },
    { "OElig",		338 },
    { "Oacute",		211 },
    { "Ocirc",		212 },
    { "Ograve",		210 },
    { "Omega",		937 },
    { "Omicron",	927 },
    { "Oslash",		216 },
    { "Otilde",		213 },
    { "Ouml",		214 },
    { "Phi",		934 },
    { "Pi",		928 },
    { "Prime",		8243 },
    { "Psi",		936 },
    { "Rho",		929 },
    { "Scaron",		352 },
    { "Sigma",		931 },
    { "THORN",		222 },
    { "Tau",		932 },
    { "Theta",		920 },
    { "Uacute",		218 },
    { "Ucirc",		219 },
    { "Ugrave",		217 },
    { "Upsilon",	933 },
    { "Uuml",		220 },
    { "Xi",		926 },
    { "Yacute",		221 },
    { "Yuml",		376 },
    { "Zeta",		918 },
    { "aacute",		225 },
    { "acirc",		226 },
    { "acute",		180 },
    { "aelig",		230 },
    { "agrave",		224 },
    { "alefsym",	8501 },
    { "alpha",		945 },
    { "amp",		'&' },
    { "and",		8743 },
    { "ang",		8736 },
    { "apos",           '\'' },
    { "aring",		229 },
    { "asymp",		8776 },
    { "atilde",		227 },
    { "auml",		228 },
    { "bdquo",		8222 },
    { "beta",		946 },
    { "brkbar",		166 },
    { "brvbar",		166 },
    { "bull",		8226 },
    { "cap",		8745 },
    { "ccedil",		231 },
    { "cedil",		184 },
    { "cent",		162 },
    { "chi",		967 },
    { "circ",		710 },
    { "clubs",		9827 },
    { "cong",		8773 },
    { "copy",		169 },
    { "crarr",		8629 },
    { "cup",		8746 },
    { "curren",		164 },
    { "dArr",		8659 },
    { "dagger",		8224 },
    { "darr",		8595 },
    { "deg",		176 },
    { "delta",		948 },
    { "diams",		9830 },
    { "die",		168 },
    { "divide",		247 },
    { "eacute",		233 },
    { "ecirc",		234 },
    { "egrave",		232 },
    { "empty",		8709 },
    { "emsp",		8195 },
    { "ensp",		8194 },
    { "epsilon",	949 },
    { "equiv",		8801 },
    { "eta",		951 },
    { "eth",		240 },
    { "euml",		235 },
    { "euro",		8364 },
    { "exist",		8707 },
    { "fnof",		402 },
    { "forall",		8704 },
    { "frac12",		189 },
    { "frac14",		188 },
    { "frac34",		190 },
    { "frasl",		8260 },
    { "gamma",		947 },
    { "ge",		8805 },
    { "gt",		'>' },
    { "hArr",		8660 },
    { "harr",		8596 },
    { "hearts",		9829 },
    { "hellip",		8230 },
    { "hibar",		175 },
    { "iacute",		237 },
    { "icirc",		238 },
    { "iexcl",		161 },
    { "igrave",		236 },
    { "image",		8465 },
    { "infin",		8734 },
    { "int",		8747 },
    { "iota",		953 },
    { "iquest",		191 },
    { "isin",		8712 },
    { "iuml",		239 },
    { "kappa",		954 },
    { "lArr",		8656 },
    { "lambda",		955 },
    { "lang",		9001 },
    { "laquo",		171 },
    { "larr",		8592 },
    { "lceil",		8968 },
    { "ldquo",		8220 },
    { "le",		8804 },
    { "lfloor",		8970 },
    { "lowast",		8727 },
    { "loz",		9674 },
    { "lrm",		8206 },
    { "lsaquo",		8249 },
    { "lsquo",		8216 },
    { "lt",		'<' },
    { "macr",		175 },
    { "mdash",		8212 },
    { "micro",		181 },
    { "middot",		183 },
    { "minus",		8722 },
    { "mu",		956 },
    { "nabla",		8711 },
    { "nbsp",		160 },
    { "ndash",		8211 },
    { "ne",		8800 },
    { "ni",		8715 },
    { "not",		172 },
    { "notin",		8713 },
    { "nsub",		8836 },
    { "ntilde",		241 },
    { "nu",		957 },
    { "oacute",		243 },
    { "ocirc",		244 },
    { "oelig",		339 },
    { "ograve",		242 },
    { "oline",		8254 },
    { "omega",		969 },
    { "omicron",	959 },
    { "oplus",		8853 },
    { "or",		8744 },
    { "ordf",		170 },
    { "ordm",		186 },
    { "oslash",		248 },
    { "otilde",		245 },
    { "otimes",		8855 },
    { "ouml",		246 },
    { "para",		182 },
    { "part",		8706 },
    { "permil",		8240 },
    { "perp",		8869 },
    { "phi",		966 },
    { "pi",		960 },
    { "piv",		982 },
    { "plusmn",		177 },
    { "pound",		163 },
    { "prime",		8242 },
    { "prod",		8719 },
    { "prop",		8733 },
    { "psi",		968 },
    { "quot",		'\"' },
    { "rArr",		8658 },
    { "radic",		8730 },
    { "rang",		9002 },
    { "raquo",		187 },
    { "rarr",		8594 },
    { "rceil",		8969 },
    { "rdquo",		8221 },
    { "real",		8476 },
    { "reg",		174 },
    { "rfloor",		8971 },
    { "rho",		961 },
    { "rlm",		8207 },
    { "rsaquo",		8250 },
    { "rsquo",		8217 },
    { "sbquo",		8218 },
    { "scaron",		353 },
    { "sdot",		8901 },
    { "sect",		167 },
    { "shy",		173 },
    { "sigma",		963 },
    { "sigmaf",		962 },
    { "sim",		8764 },
    { "spades",		9824 },
    { "sub",		8834 },
    { "sube",		8838 },
    { "sum",		8721 },
    { "sup",		8835 },
    { "sup1",		185 },
    { "sup2",		178 },
    { "sup3",		179 },
    { "supe",		8839 },
    { "szlig",		223 },
    { "tau",		964 },
    { "there4",		8756 },
    { "theta",		952 },
    { "thetasym",	977 },
    { "thinsp",		8201 },
    { "thorn",		254 },
    { "tilde",		732 },
    { "times",		215 },
    { "trade",		8482 },
    { "uArr",		8657 },
    { "uacute",		250 },
    { "uarr",		8593 },
    { "ucirc",		251 },
    { "ugrave",		249 },
    { "uml",		168 },
    { "upsih",		978 },
    { "upsilon",	965 },
    { "uuml",		252 },
    { "weierp",		8472 },
    { "xi",		958 },
    { "yacute",		253 },
    { "yen",		165 },
    { "yuml",		255 },
    { "zeta",		950 },
    { "zwj",		8205 },
    { "zwnj",		8204 }
  };


 /*
  * Do a binary search for the named entity...
  */

  first = 0;
  last  = (int)(sizeof(entities) / sizeof(entities[0]) - 1);

  while ((last - first) > 1)
  {
    current = (first + last) / 2;

    if ((diff = strcmp(name, entities[current].name)) == 0)
      return (entities[current].val);
    else if (diff < 0)
      last = current;
    else
      first = current;
  }

 /*
  * If we get here, there is a small chance that there is still
  * a match; check first and last...
  */

  if (!strcmp(name, entities[first].name))
    return (entities[first].val);
  else if (!strcmp(name, entities[last].name))
    return (entities[last].val);
  else
    return (-1);
}


/*
 * End of "$Id: mxml-entity.c 451 2014-01-04 21:50:06Z msweet $".
 */
