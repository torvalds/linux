#ifndef XSTRTOL_H_
# define XSTRTOL_H_ 1

# if HAVE_INTTYPES_H
#  include <inttypes.h> /* for uintmax_t */
# endif

# ifndef PARAMS
#  if defined PROTOTYPES || (defined __STDC__ && __STDC__)
#   define PARAMS(Args) Args
#  else
#   define PARAMS(Args) ()
#  endif
# endif

# ifndef _STRTOL_ERROR
enum strtol_error
  {
    LONGINT_OK, LONGINT_INVALID, LONGINT_INVALID_SUFFIX_CHAR, LONGINT_OVERFLOW
  };
typedef enum strtol_error strtol_error;
# endif

# define _DECLARE_XSTRTOL(name, type) \
  strtol_error \
    name PARAMS ((const char *s, char **ptr, int base, \
		  type *val, const char *valid_suffixes));
_DECLARE_XSTRTOL (xstrtol, long int)
_DECLARE_XSTRTOL (xstrtoul, unsigned long int)
_DECLARE_XSTRTOL (xstrtoumax, uintmax_t)

# define _STRTOL_ERROR(Exit_code, Str, Argument_type_string, Err)	\
  do									\
    {									\
      switch ((Err))							\
	{								\
	case LONGINT_OK:						\
	  abort ();							\
									\
	case LONGINT_INVALID:						\
	  error ((Exit_code), 0, "invalid %s `%s'",			\
		 (Argument_type_string), (Str));			\
	  break;							\
									\
	case LONGINT_INVALID_SUFFIX_CHAR:				\
	  error ((Exit_code), 0, "invalid character following %s `%s'",	\
		 (Argument_type_string), (Str));			\
	  break;							\
									\
	case LONGINT_OVERFLOW:						\
	  error ((Exit_code), 0, "%s `%s' too large",			\
		 (Argument_type_string), (Str));			\
	  break;							\
	}								\
    }									\
  while (0)

# define STRTOL_FATAL_ERROR(Str, Argument_type_string, Err)		\
  _STRTOL_ERROR (2, Str, Argument_type_string, Err)

# define STRTOL_FAIL_WARN(Str, Argument_type_string, Err)		\
  _STRTOL_ERROR (0, Str, Argument_type_string, Err)

#endif /* not XSTRTOL_H_ */
