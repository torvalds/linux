/*	$OpenBSD: monot.c,v 1.2 2021/05/29 10:35:56 bluhm Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* monot.c
   Floating point function test vectors.

   Arguments and function values are synthesized for NPTS points in
   the vicinity of each given tabulated test point.  The points are
   chosen to be near and on either side of the likely function algorithm
   domain boundaries.  Since the function programs change their methods
   at these points, major coding errors or monotonicity failures might be
   detected.

   August, 1998
   S. L. Moshier  */


#include <stdio.h>

/* Avoid including math.h.  */
double frexp (double, int *);
double ldexp (double, int);

/* Number of test points to generate on each side of tabulated point.  */
#define NPTS 100

/* Functions of one variable.  */
double exp (double);
double log (double);
double sin (double);
double cos (double);
double tan (double);
double atan (double);
double asin (double);
double acos (double);
double sinh (double);
double cosh (double);
double tanh (double);
double asinh (double);
double acosh (double);
double atanh (double);
double gamma (double);
double fabs (double);
double floor (double);
#if 0
double polylog (int, double);
#endif

struct oneargument
  {
    char *name;			/* Name of the function. */
    double (*func) (double);
    double arg1;		/* Function argument, assumed exact.  */
    double answer1;		/* Exact, close to function value.  */
    double answer2;		/* answer1 + answer2 has extended precision. */
    double derivative;		/* dy/dx evaluated at x = arg1. */
    int thresh;			/* Error report threshold. 2 = 1 ULP approx. */
  };

/* Add this to error threshold test[i].thresh.  */
#define OKERROR 0

/* Unit of relative error in test[i].thresh.  */
static double MACHEP = 1.1102230246251565404236316680908203125e-16;


/* extern double MACHEP; */

#if 0
double w_polylog_3 (double x) {return polylog (3, x);}
#endif

static struct oneargument test1[] =
{
  {"exp", exp, 1.0, 2.7182769775390625,
   4.85091998273536028747e-6, 2.71828182845904523536, 2},
  {"exp", exp, -1.0, 3.678741455078125e-1,
    5.29566362982159552377e-6, 3.678794411714423215955e-1, 2},
  {"exp", exp, 0.5, 1.648712158203125,
    9.1124970031468486507878e-6, 1.64872127070012814684865, 2},
  {"exp", exp, -0.5, 6.065216064453125e-1,
    9.0532673209236037995e-6, 6.0653065971263342360e-1, 2},
  {"exp", exp, 2.0, 7.3890533447265625,
    2.75420408772723042746e-6, 7.38905609893065022723, 2},
  {"exp", exp, -2.0, 1.353302001953125e-1,
    5.08304130019189399949e-6, 1.3533528323661269189e-1, 2},
  {"log", log, 1.41421356237309492343, 3.465728759765625e-1,
   7.1430341006605745676897e-7, 7.0710678118654758708668e-1, 2},
  {"log", log, 7.07106781186547461715e-1, -3.46588134765625e-1,
   1.45444856522566402246e-5, 1.41421356237309517417, 2},
  {"sin", sin, 7.85398163397448278999e-1, 7.0709228515625e-1,
   1.4496030297502751942956e-5, 7.071067811865475460497e-1, 2},
  {"sin", sin, -7.85398163397448501044e-1, -7.071075439453125e-1,
   7.62758764840238811175e-7, 7.07106781186547389040e-1, 2},
  {"sin", sin, 1.570796326794896558, 9.999847412109375e-1,
   1.52587890625e-5, 6.12323399573676588613e-17, 2},
  {"sin", sin, -1.57079632679489678004, -1.0,
   1.29302922820150306903e-32, -1.60812264967663649223e-16, 2},
  {"sin", sin, 4.712388980384689674, -1.0,
   1.68722975549458979398e-32, -1.83697019872102976584e-16, 2},
  {"sin", sin, -4.71238898038468989604, 9.999847412109375e-1,
   1.52587890625e-5, 3.83475850529283315008e-17, 2},
  {"cos", cos, 3.92699081698724139500E-1, 9.23873901367187500000E-1,
   5.63114409926198633370E-6, -3.82683432365089757586E-1, 2},
  {"cos", cos, 7.85398163397448278999E-1, 7.07092285156250000000E-1,
   1.44960302975460497458E-5, -7.07106781186547502752E-1, 2},
  {"cos", cos, 1.17809724509617241850E0, 3.82675170898437500000E-1,
   8.26146665231415693919E-6, -9.23879532511286738554E-1, 2},
  {"cos", cos, 1.96349540849362069750E0, -3.82690429687500000000E-1,
   6.99732241029898567203E-6, -9.23879532511286785419E-1, 2},
  {"cos", cos, 2.35619449019234483700E0, -7.07107543945312500000E-1,
   7.62758765040545859856E-7, -7.07106781186547589348E-1, 2},
  {"cos", cos, 2.74889357189106897650E0, -9.23889160156250000000E-1,
   9.62764496328487887036E-6, -3.82683432365089870728E-1, 2},
  {"cos", cos, 3.14159265358979311600E0, -1.00000000000000000000E0,
   7.49879891330928797323E-33, -1.22464679914735317723E-16, 2},
  {"tan", tan, 7.85398163397448278999E-1, 9.999847412109375e-1,
   1.52587890624387676600E-5, 1.99999999999999987754E0, 2},
  {"tan", tan, 1.17809724509617241850E0, 2.41419982910156250000E0,
   1.37332715322352112604E-5, 6.82842712474618858345E0, 2},
  {"tan", tan, 1.96349540849362069750E0, -2.41421508789062500000E0,
   1.52551752942854759743E-6, 6.82842712474619262118E0, 2},
  {"tan", tan, 2.35619449019234483700E0, -1.00001525878906250000E0,
   1.52587890623163029801E-5, 2.00000000000000036739E0, 2},
  {"tan", tan, 2.74889357189106897650E0, -4.14215087890625000000E-1,
   1.52551752982565655126E-6, 1.17157287525381000640E0, 2},
  {"atan", atan, 4.14213562373094923430E-1, 3.92684936523437500000E-1,
   1.41451752865477964149E-5, 8.53553390593273837869E-1, 2},
  {"atan", atan, 1.0, 7.85385131835937500000E-1,
   1.30315615108096156608E-5, 0.5, 2},
  {"atan", atan, 2.41421356237309492343E0, 1.17808532714843750000E0,
   1.19179477349460632350E-5, 1.46446609406726250782E-1, 2},
  {"atan", atan, -2.41421356237309514547E0, -1.17810058593750000000E0,
   3.34084132752141908545E-6, 1.46446609406726227789E-1, 2},
  {"atan", atan, -1.0, -7.85400390625000000000E-1,
   2.22722755169038433915E-6, 0.5, 2},
  {"atan", atan, -4.14213562373095145475E-1, -3.92700195312500000000E-1,
   1.11361377576267665972E-6, 8.53553390593273703853E-1, 2},
  {"asin", asin, 3.82683432365089615246E-1, 3.92684936523437500000E-1,
   1.41451752864854321970E-5, 1.08239220029239389286E0, 2},
  {"asin", asin, 0.5, 5.23590087890625000000E-1,
   8.68770767387307710723E-6, 1.15470053837925152902E0, 2},
  {"asin", asin, 7.07106781186547461715E-1, 7.85385131835937500000E-1,
   1.30315615107209645016E-5, 1.41421356237309492343E0, 2},
  {"asin", asin, 9.23879532511286738483E-1, 1.17808532714843750000E0,
   1.19179477349183147612E-5, 2.61312592975275276483E0, 2},
  {"asin", asin, -0.5, -5.23605346679687500000E-1,
   6.57108138862692289277E-6, 1.15470053837925152902E0, 2},
  {"acos", acos, 1.95090322016128192573E-1, 1.37443542480468750000E0,
   1.13611408471185777914E-5, -1.01959115820831832232E0, 2},
  {"acos", acos, 3.82683432365089615246E-1, 1.17808532714843750000E0,
   1.19179477351337991247E-5, -1.08239220029239389286E0, 2},
  {"acos", acos, 0.5, 1.04719543457031250000E0,
   2.11662628524615421446E-6, -1.15470053837925152902E0, 2},
  {"acos", acos, 7.07106781186547461715E-1, 7.85385131835937500000E-1,
   1.30315615108982668201E-5, -1.41421356237309492343E0, 2},
  {"acos", acos, 9.23879532511286738483E-1, 3.92684936523437500000E-1,
   1.41451752867009165605E-5, -2.61312592975275276483E0, 2},
  {"acos", acos, 9.80785280403230430579E-1, 1.96334838867187500000E-1,
   1.47019821746724723933E-5, -5.12583089548300990774E0, 2},
  {"acos", acos, -0.5, 2.09439086914062500000E0,
   4.23325257049230842892E-6, -1.15470053837925152902E0, 2},
  {"sinh", sinh, 1.0, 1.17518615722656250000E0,
   1.50364172389568823819E-5, 1.54308063481524377848E0, 2},
  {"sinh", sinh, 7.09089565712818057364E2, 4.49423283712885057274E307,
   4.25947714184369757620E208, 4.49423283712885057274E307, 2},
  {"sinh", sinh, 2.22044604925031308085E-16, 0.00000000000000000000E0,
   2.22044604925031308085E-16, 1.00000000000000000000E0, 2},
  {"cosh", cosh, 7.09089565712818057364E2, 4.49423283712885057274E307,
   4.25947714184369757620E208, 4.49423283712885057274E307, 2},
  {"cosh", cosh, 1.0, 1.54307556152343750000E0,
   5.07329180627847790562E-6, 1.17520119364380145688E0, 2},
  {"cosh", cosh, 0.5, 1.12762451171875000000E0,
   1.45348763078522622516E-6, 5.21095305493747361622E-1, 2},
  /* tanh in OpenBSD libm has less precession, adapt expectation from 2 to 3 */
  {"tanh", tanh, 0.5, 4.62112426757812500000E-1,
   4.73050219725850231848E-6, 7.86447732965927410150E-1, 3},
  {"tanh", tanh, 5.49306144334054780032E-1, 4.99984741210937500000E-1,
   1.52587890624507506378E-5, 7.50000000000000049249E-1, 2},
  {"tanh", tanh, 0.625, 5.54595947265625000000E-1,
   3.77508375729399903910E-6, 6.92419147969988069631E-1, 3},
  {"asinh", asinh, 0.5, 4.81201171875000000000E-1,
   1.06531846034474977589E-5, 8.94427190999915878564E-1, 2},
  {"asinh", asinh, 1.0, 8.81362915039062500000E-1,
   1.06719804805252326093E-5, 7.07106781186547524401E-1, 2},
  {"asinh", asinh, 2.0, 1.44363403320312500000E0,
   1.44197568534249327674E-6, 4.47213595499957939282E-1, 2},
  {"acosh", acosh, 2.0, 1.31695556640625000000E0,
   2.33051856670862504635E-6, 5.77350269189625764509E-1, 2},
  {"acosh", acosh, 1.5, 9.62417602539062500000E-1,
   6.04758014439499551783E-6, 8.94427190999915878564E-1, 2},
  {"acosh", acosh, 1.03125, 2.49343872070312500000E-1,
   9.62177257298785143908E-6, 3.96911150685467059809E0, 2},
 {"atanh", atanh, 0.5, 5.49301147460937500000E-1,
   4.99687311734569762262E-6, 1.33333333333333333333E0, 2},
#if 0
  {"polylog_3", w_polylog_3, 0.875, 1.01392710208892822265625,
   1.21106784918910854520955967e-8, 1.4149982649102631253520580, 2},
  {"polylog_3", w_polylog_3, 8.0000000000000004440892e-1,
   9.10605847835540771484375e-1, 7.570301035551561731571421485488e-9,
   1.343493250010310486342647, 2},
#endif
#if 0
  {"gamma", gamma, 1.0, 1.0,
   0.0, -5.772156649015328606e-1, 2},
  {"gamma", gamma, 2.0, 1.0,
   0.0, 4.2278433509846713939e-1, 2},
  {"gamma", gamma, 3.0, 2.0,
   0.0, 1.845568670196934279, 2},
  {"gamma", gamma, 4.0, 6.0,
   0.0, 7.536706010590802836, 2},
#endif
  {"null", NULL, 0.0, 0.0, 0.0, 2},
};

/* These take care of extra-precise floating point register problems.  */
static volatile double volat1;
static volatile double volat2;


/* Return the next nearest floating point value to X
   in the direction of UPDOWN (+1 or -1).
   (Fails if X is denormalized.)  */

static double
nextval (x, updown)
     double x;
     int updown;
{
  double m;
  int i;

  volat1 = x;
  m = 0.25 * MACHEP * volat1 * updown;
  volat2 = volat1 + m;
  if (volat2 != volat1)
    printf ("successor failed\n");

  for (i = 2; i < 10; i++)
    {
      volat2 = volat1 + i * m;
      if (volat1 != volat2)
	return volat2;
    }

  printf ("nextval failed\n");
  return volat1;
}




int
monot ()
{
  double (*fun1) (double);
  int i, j, errs, tests;
  double x, x0, y, dy, err;

  /* Set math coprocessor to double precision.  */
  /*  dprec (); */
  errs = 0;
  tests = 0;
  i = 0;

  for (;;)
    {
      fun1 = test1[i].func;
      if (fun1 == NULL)
	break;
      volat1 = test1[i].arg1;
      x0 = volat1;
      x = volat1;
      for (j = 0; j <= NPTS; j++)
	{
	  volat1 = x - x0;
	  dy = volat1 * test1[i].derivative;
	  dy = test1[i].answer2 + dy;
	  volat1 = test1[i].answer1 + dy;
	  volat2 = (*(fun1)) (x);
	  if (volat2 != volat1)
	    {
	      /* Report difference between program result
		 and extended precision function value.  */
	      err = volat2 - test1[i].answer1;
	      err = err - dy;
	      err = err / volat1;
	      if (fabs (err) > ((OKERROR + test1[i].thresh) * MACHEP))
		{
		  printf ("%d %s(%.16e) = %.16e, rel err = %.3e\n",
			  j, test1[i].name, x, volat2, err);
		  errs += 1;
		}
	    }
	  x = nextval (x, 1);
	  tests += 1;
	}

      x = x0;
      x = nextval (x, -1);
      for (j = 1; j < NPTS; j++)
	{
	  volat1 = x - x0;
	  dy = volat1 * test1[i].derivative;
	  dy = test1[i].answer2 + dy;
	  volat1 = test1[i].answer1 + dy;
	  volat2 = (*(fun1)) (x);
	  if (volat2 != volat1)
	    {
	      err = volat2 - test1[i].answer1;
	      err = err - dy;
	      err = err / volat1;
	      if (fabs (err) > ((OKERROR + test1[i].thresh) * MACHEP))
		{
		  printf ("%d %s(%.16e) = %.16e, rel err = %.3e\n",
			  j, test1[i].name, x, volat2, err);
		  errs += 1;
		}
	    }
	  x = nextval (x, -1);
	  tests += 1;
	}
      i += 1;
    }
  printf ("%d errors in %d tests\n", errs, tests);
  return (errs);
}
