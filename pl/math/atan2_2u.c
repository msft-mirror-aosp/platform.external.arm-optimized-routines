/*
 * Double-precision scalar atan2(x) function.
 *
 * Copyright (c) 2021-2022, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

#include <stdbool.h>

#include "math_config.h"
#include "atan_common.h"

#define Pi (0x1.921fb54442d18p+1)
#define PiOver2 (0x1.921fb54442d18p+0)
#define PiOver4 (0x1.921fb54442d18p-1)
#define SignMask (0x8000000000000000)
#define ExpMask (0x7ff0000000000000)

/* We calculate atan2 by P(n/d), where n and d are similar to the input
   arguments, and P is a polynomial. Evaluating P(x) requires calculating x^8,
   which may underflow if n and d have very different magnitude.
   POW8_EXP_UFLOW_BOUND is the lower bound of the difference in exponents of n
   and d for which P underflows, and is used to special-case such inputs.  */
#define POW8_EXP_UFLOW_BOUND 62

static inline int64_t
biased_exponent (double f)
{
  uint64_t fi = asuint64 (f);
  int64_t ex = (fi & ExpMask) >> 52;
  if (unlikely (ex == 0))
    {
      /* Subnormal case - we still need to get the exponent right for subnormal
	 numbers as division may take us back inside the normal range.  */
      return ex - __builtin_clz (fi << 12);
    }
  return ex;
}

/* Fast implementation of scalar atan2. Errors are greatest when y and
   x are reasonably close together. Maximum observed error is 2.0 ulps:
   atan2(0x1.8d9621df2f329p+2, 0x1.884cf49437972p+2)
   got 0x1.958cd0e8c618bp-1 want 0x1.958cd0e8c618dp-1.  */
double
atan2 (double y, double x)
{
  uint64_t ix = asuint64 (x);
  uint64_t iy = asuint64 (y);

  uint64_t sign_x = ix & SignMask;
  uint64_t sign_y = iy & SignMask;

  uint64_t iax = ix & ~SignMask;
  uint64_t iay = iy & ~SignMask;

  bool xisnan = isnan (x);
  if (unlikely (isnan (y) && !xisnan))
    return __math_invalid (y);
  if (unlikely (xisnan))
    return __math_invalid (x);

  /* m = 2 * sign(x) + sign(y).  */
  uint32_t m = ((iy >> 63) & 1) | ((ix >> 62) & 2);

  int64_t exp_diff = biased_exponent (x) - biased_exponent (y);

  /* y = 0.  */
  if (iay == 0)
    {
      switch (m)
	{
	case 0:
	case 1:
	  return y; /* atan(+-0,+anything)=+-0.  */
	case 2:
	  return Pi; /* atan(+0,-anything) = pi.  */
	case 3:
	  return -Pi; /* atan(-0,-anything) =-pi.  */
	}
    }
  /* Special case for (x, y) either on or very close to the y axis. Either x =
     0, or y is much larger than x (difference in exponents >=
     POW8_EXP_UFLOW_BOUND).  */
  if (unlikely (iax == 0 || exp_diff <= -POW8_EXP_UFLOW_BOUND))
    return sign_y ? -PiOver2 : PiOver2;

  /* Special case for either x is INF or (x, y) is very close to x axis and x is
     negative.  */
  if (unlikely (iax == 0x7ff0000000000000
		|| (exp_diff >= POW8_EXP_UFLOW_BOUND && m >= 2)))
    {
      if (iay == 0x7ff0000000000000)
	{
	  switch (m)
	    {
	    case 0:
	      return PiOver4; /* atan(+INF,+INF).  */
	    case 1:
	      return -PiOver4; /* atan(-INF,+INF).  */
	    case 2:
	      return 3.0 * PiOver4; /* atan(+INF,-INF).  */
	    case 3:
	      return -3.0 * PiOver4; /* atan(-INF,-INF).  */
	    }
	}
      else
	{
	  switch (m)
	    {
	    case 0:
	      return 0.0; /* atan(+...,+INF).  */
	    case 1:
	      return -0.0; /* atan(-...,+INF).  */
	    case 2:
	      return Pi; /* atan(+...,-INF).  */
	    case 3:
	      return -Pi; /* atan(-...,-INF).  */
	    }
	}
    }
  /* y is INF.  */
  if (iay == 0x7ff0000000000000)
    return sign_y ? -PiOver2 : PiOver2;

  uint64_t sign_xy = sign_x ^ sign_y;

  double ax = asdouble (iax);
  double ay = asdouble (iay);
  uint64_t pred_aygtax = (ay > ax);

  /* Set up z for call to atan.  */
  double n = pred_aygtax ? -ax : ay;
  double d = pred_aygtax ? ay : ax;
  double z = n / d;

  double ret;
  if (unlikely (m < 2 && exp_diff >= POW8_EXP_UFLOW_BOUND))
    {
      /* If (x, y) is very close to x axis and x is positive, the polynomial
	 will underflow and evaluate to z.  */
      ret = z;
    }
  else
    {
      /* Work out the correct shift.  */
      double shift = sign_x ? -2.0 : 0.0;
      shift = pred_aygtax ? shift + 1.0 : shift;
      shift *= PiOver2;

      ret = eval_poly (z, z, shift);
    }

  /* Account for the sign of x and y.  */
  return asdouble (asuint64 (ret) ^ sign_xy);
}
