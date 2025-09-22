#include <ansidecl.h>

#ifdef __IEEE_BIG_ENDIAN

typedef union 
{
  double value;
  struct 
  {
    unsigned int sign : 1;
    unsigned int exponent: 11;
    unsigned int fraction0:4;
    unsigned int fraction1:16;
    unsigned int fraction2:16;
    unsigned int fraction3:16;
    
  } number;
  struct 
  {
    unsigned int sign : 1;
    unsigned int exponent: 11;
    unsigned int quiet:1;
    unsigned int function0:3;
    unsigned int function1:16;
    unsigned int function2:16;
    unsigned int function3:16;
  } nan;
  struct 
  {
    unsigned long msw;
    unsigned long lsw;
  } parts;
    long aslong[2];
} __ieee_double_shape_type;

#endif

#ifdef __IEEE_LITTLE_ENDIAN

typedef union 
{
  double value;
  struct 
  {
#ifdef __SMALL_BITFIELDS
    unsigned int fraction3:16;
    unsigned int fraction2:16;
    unsigned int fraction1:16;
    unsigned int fraction0: 4;
#else
    unsigned int fraction1:32;
    unsigned int fraction0:20;
#endif
    unsigned int exponent :11;
    unsigned int sign     : 1;
  } number;
  struct 
  {
#ifdef __SMALL_BITFIELDS
    unsigned int function3:16;
    unsigned int function2:16;
    unsigned int function1:16;
    unsigned int function0:3;
#else
    unsigned int function1:32;
    unsigned int function0:19;
#endif
    unsigned int quiet:1;
    unsigned int exponent: 11;
    unsigned int sign : 1;
  } nan;
  struct 
  {
    unsigned long lsw;
    unsigned long msw;
  } parts;

  long aslong[2];

} __ieee_double_shape_type;

#endif

#ifdef __IEEE_BIG_ENDIAN
typedef union
{
  float value;
  struct 
  {
    unsigned int sign : 1;
    unsigned int exponent: 8;
    unsigned int fraction0: 7;
    unsigned int fraction1: 16;
  } number;
  struct 
  {
    unsigned int sign:1;
    unsigned int exponent:8;
    unsigned int quiet:1;
    unsigned int function0:6;
    unsigned int function1:16;
  } nan;
  long p1;
  
} __ieee_float_shape_type;
#endif

#ifdef __IEEE_LITTLE_ENDIAN
typedef union
{
  float value;
  struct 
  {
    unsigned int fraction0: 7;
    unsigned int fraction1: 16;
    unsigned int exponent: 8;
    unsigned int sign : 1;
  } number;
  struct 
  {
    unsigned int function1:16;
    unsigned int function0:6;
    unsigned int quiet:1;
    unsigned int exponent:8;
    unsigned int sign:1;
  } nan;
  long p1;
  
} __ieee_float_shape_type;
#endif

#if defined(__IEEE_BIG_ENDIAN) || defined(__IEEE_LITTLE_ENDIAN)

double
copysign (double x, double y)
{
  __ieee_double_shape_type a,b;
  b.value = y;  
  a.value = x;
  a.number.sign =b.number.sign;
  return a.value;
}

#else

double
copysign (double x, double y)
{
  if ((x < 0 && y > 0) || (x > 0 && y < 0))
    return -x;
  return x;
}

#endif
