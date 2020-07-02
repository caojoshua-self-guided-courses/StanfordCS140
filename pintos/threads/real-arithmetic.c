#include "real-arithmetic.h"

/* Arithmetic for fixed-point real numbers. Numbers are represented in
 * 17.14 fixed-pointer number representation. Details here: 
 * http://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/pintos/pintos_7.html#SEC131 */

#define p 17       /* bits for numerator. */
#define q 14       /* bits for denominator. */
#define f (1 << q)   /* convert between integer and fixed-point. */

/* Convert integer n to fixed-point. */
int
int_to_fp (int n)
{
  return n * f;
}

/* Convert fixed-point number x to integer. If round_to_nearest is true, round to
 * the nearest whole number. Otherwise, round towards zero. */
int
fp_to_int (int x, bool round_to_nearest)
{
  if (round_to_nearest)
  {
    if (x >= 0)
      return (x + (f / 2)) / f;
    return (x - (f / 2)) / f;
  }
  return x / f;
}

/* Multiply two fixed-point numbers */
int
fp_mul (int x, int y)
{
  return ((int64_t) x) * y / f;
}

/* Divide two fixed-pionter numbers. */
int
fp_div (int x, int y)
{
  return ((int64_t) x) * f / y;
}
