/*	$OpenBSD: monotl.c,v 1.2 2011/07/08 16:49:05 martynas Exp $	*/

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

/* monotl.c
   Floating point function test vectors.

   Arguments and function values are synthesized for NPTS points in
   the vicinity of each given tabulated test point.  The points are
   chosen to be near and on either side of the likely function algorithm
   domain boundaries.  Since the function programs change their methods
   at these points, major coding errors or monotonicity failures might be
   detected.

   August, 1998
   S. L. Moshier  */

#include <float.h>

#if	LDBL_MANT_DIG == 64
/* Unit of error tolerance in test[i].thresh.  */
static long double MACHEPL =
  5.42101086242752217003726400434970855712890625E-20L;
/* How many times the above error to allow before printing a complaint.
   If TOL < 0, consider absolute error instead of relative error. */
#define TOL 8
/* Number of test points to generate on each side of tabulated point.  */
#define NPTS 100



#include <stdio.h>


/* Avoid including math.h.  */
long double frexpl (long double, int *);
long double ldexpl (long double, int);

/* Functions of one variable.  */
long double expl (long double);
long double logl (long double);
long double sinl (long double);
long double cosl (long double);
long double tanl (long double);
long double atanl (long double);
long double asinl (long double);
long double acosl (long double);
long double sinhl (long double);
long double coshl (long double);
long double tanhl (long double);
long double asinhl (long double);
long double acoshl (long double);
long double atanhl (long double);
long double lgammal (long double);
long double tgammal (long double);
long double fabsl (long double);
long double floorl (long double);
long double j0l (long double);
long double y0l (long double);
long double j1l (long double);
long double y1l (long double);

/* Data structure of the test.  */
struct oneargument
  {
    char *name;			/* Name of the function. */
    long double (*func) (long double); /* Function call.  */
    long double arg1;		/* Function argument, assumed exact.  */
    long double answer1;	/* Exact number, close to function value.  */
    long double answer2;	/* answer1 + answer2 has extended precision. */
    long double derivative;	/* dy/dx evaluated at x = arg1. */
    /* Error report threshold. 2 => 1 ulp approximately
       if thresh < 0 then consider absolute error instead of relative error. */
    int thresh;

  };



static struct oneargument test1[] =
{
  {"exp", expl, 1.0L, 2.7182769775390625L,
   4.85091998273536028747e-6L, 2.71828182845904523536L, TOL},
  {"exp", expl, -1.0L, 3.678741455078125e-1L,
    5.29566362982159552377e-6L, 3.678794411714423215955e-1L, TOL},
  {"exp", expl, 0.5L, 1.648712158203125L,
    9.1124970031468486507878e-6L, 1.64872127070012814684865L, TOL},
  {"exp", expl, -0.5L, 6.065216064453125e-1L,
    9.0532673209236037995e-6L, 6.0653065971263342360e-1L, TOL},
  {"exp", expl, 2.0L, 7.3890533447265625L,
    2.75420408772723042746e-6L, 7.38905609893065022723L, TOL},
  {"exp", expl, -2.0L, 1.353302001953125e-1L,
    5.08304130019189399949e-6L, 1.3533528323661269189e-1L, TOL},
  {"log", logl, 1.41421356237309492343L, 3.465728759765625e-1L,
   7.1430341006605745676897e-7L, 7.0710678118654758708668e-1L, TOL},
  {"log", logl, 7.07106781186547461715e-1L, -3.46588134765625e-1L,
   1.45444856522566402246e-5L, 1.41421356237309517417L, TOL},
  {"sin", sinl, 7.85398163397448278999e-1L, 7.0709228515625e-1L,
   1.4496030297502751942956e-5L, 7.071067811865475460497e-1L, TOL},
  {"sin", sinl, -7.85398163397448501044e-1L, -7.071075439453125e-1L,
   7.62758764840238811175e-7L, 7.07106781186547389040e-1L, TOL},
  {"sin", sinl, 1.570796326794896558L, 9.999847412109375e-1L,
   1.52587890625e-5L, 6.12323399573676588613e-17L, TOL},
  {"sin", sinl, -1.57079632679489678004L, -1.0L,
   1.29302922820150306903e-32L, -1.60812264967663649223e-16L, TOL},
  {"sin", sinl, 4.712388980384689674L, -1.0L,
   1.68722975549458979398e-32L, -1.83697019872102976584e-16L, TOL},
  {"sin", sinl, -4.71238898038468989604L, 9.999847412109375e-1L,
   1.52587890625e-5L, 3.83475850529283315008e-17L, TOL},
  {"cos", cosl, 3.92699081698724139500E-1L, 9.23873901367187500000E-1L,
   5.63114409926198633370E-6L, -3.82683432365089757586E-1L, TOL},
  {"cos", cosl, 7.85398163397448278999E-1L, 7.07092285156250000000E-1L,
   1.44960302975460497458E-5L, -7.07106781186547502752E-1L, TOL},
  {"cos", cosl, 1.17809724509617241850E0L, 3.82675170898437500000E-1L,
   8.26146665231415693919E-6L, -9.23879532511286738554E-1L, TOL},
  {"cos", cosl, 1.96349540849362069750E0L, -3.82690429687500000000E-1L,
   6.99732241029898567203E-6L, -9.23879532511286785419E-1L, TOL},
  {"cos", cosl, 2.35619449019234483700E0L, -7.07107543945312500000E-1L,
   7.62758765040545859856E-7L, -7.07106781186547589348E-1L, TOL},
  {"cos", cosl, 2.74889357189106897650E0L, -9.23889160156250000000E-1L,
   9.62764496328487887036E-6L, -3.82683432365089870728E-1L, TOL},
  {"cos", cosl, 3.14159265358979311600E0L, -1.00000000000000000000E0L,
   7.49879891330928797323E-33L, -1.22464679914735317723E-16L, TOL},
  {"tan", tanl, 7.85398163397448278999E-1L, 9.999847412109375e-1L,
   1.52587890624387676600E-5L, 1.99999999999999987754E0L, TOL},
  {"tan", tanl, 1.17809724509617241850E0L, 2.41419982910156250000E0L,
   1.37332715322352112604E-5L, 6.82842712474618858345E0L, TOL},
  {"tan", tanl, 1.96349540849362069750E0L, -2.41421508789062500000E0L,
   1.52551752942854759743E-6L, 6.82842712474619262118E0L, TOL},
  {"tan", tanl, 2.35619449019234483700E0L, -1.00001525878906250000E0L,
   1.52587890623163029801E-5L, 2.00000000000000036739E0L, TOL},
  {"tan", tanl, 2.74889357189106897650E0L, -4.14215087890625000000E-1L,
   1.52551752982565655126E-6L, 1.17157287525381000640E0L, TOL},
  {"atan", atanl, 4.14213562373094923430E-1L, 3.92684936523437500000E-1L,
   1.41451752865477964149E-5L, 8.53553390593273837869E-1L, TOL},
  {"atan", atanl, 1.0L, 7.85385131835937500000E-1L,
   1.30315615108096156608E-5L, 0.5L, TOL},
  {"atan", atanl, 2.41421356237309492343E0L, 1.17808532714843750000E0L,
   1.19179477349460632350E-5L, 1.46446609406726250782E-1L, TOL},
  {"atan", atanl, -2.41421356237309514547E0L, -1.17810058593750000000E0L,
   3.34084132752141908545E-6L, 1.46446609406726227789E-1L, TOL},
  {"atan", atanl, -1.0L, -7.85400390625000000000E-1L,
   2.22722755169038433915E-6L, 0.5L, TOL},
  {"atan", atanl, -4.14213562373095145475E-1L, -3.92700195312500000000E-1L,
   1.11361377576267665972E-6L, 8.53553390593273703853E-1L, TOL},
  {"asin", asinl, 3.82683432365089615246E-1L, 3.92684936523437500000E-1L,
   1.41451752864854321970E-5L, 1.08239220029239389286E0L, TOL},
  {"asin", asinl, 0.5L, 5.23590087890625000000E-1L,
   8.68770767387307710723E-6L, 1.15470053837925152902E0L, TOL},
  {"asin", asinl, 7.07106781186547461715E-1L, 7.85385131835937500000E-1L,
   1.30315615107209645016E-5L, 1.41421356237309492343E0L, TOL},
  {"asin", asinl, 9.23879532511286738483E-1L, 1.17808532714843750000E0L,
   1.19179477349183147612E-5L, 2.61312592975275276483E0L, TOL},
  {"asin", asinl, -0.5L, -5.23605346679687500000E-1L,
   6.57108138862692289277E-6L, 1.15470053837925152902E0L, TOL},
  {"asin", asinl, 1.16415321826934814453125e-10L,
   1.16415321826934814453125e-10L, 2.629536350736706018055e-31L,
   1.0000000000000000000067762L, TOL},
  {"asin", asinl, 1.0000000000000000000183779E-10L,
   9.9999999979890480394928431E-11L, 2.0109519607076028264987890E-20L,
   1.0L, TOL},
  {"asin", asinl, 1.0000000000000000000007074E-8L,
   9.9999999999948220585910263E-9L, 5.1781080827147808155022014E-21L,
   1.0L, TOL},
  {"asin", asinl, 0.97499847412109375L, 1.346710205078125L,
   3.969526822009922560999e-6L, 4.500216008585875735254L, TOL},
  {"acos", acosl, 1.95090322016128192573E-1L, 1.37443542480468750000E0L,
   1.13611408471185777914E-5L, -1.01959115820831832232E0L, TOL},
  {"acos", acosl, 3.82683432365089615246E-1L, 1.17808532714843750000E0L,
   1.19179477351337991247E-5L, -1.08239220029239389286E0L, TOL},
  {"acos", acosl, 0.5L, 1.04719543457031250000E0L,
   2.11662628524615421446E-6L, -1.15470053837925152902E0L, TOL},
  {"acos", acosl, 7.07106781186547461715E-1L, 7.85385131835937500000E-1L,
   1.30315615108982668201E-5L, -1.41421356237309492343E0L, TOL},
  {"acos", acosl, 9.23879532511286738483E-1L, 3.92684936523437500000E-1L,
   1.41451752867009165605E-5L, -2.61312592975275276483E0L, TOL},
  {"acos", acosl, 9.80785280403230430579E-1L, 1.96334838867187500000E-1L,
   1.47019821746724723933E-5L, -5.12583089548300990774E0L, TOL},
  {"acos", acosl, -0.5L, 2.09439086914062500000E0L,
   4.23325257049230842892E-6L, -1.15470053837925152902E0L, TOL},
  {"sinh", sinhl, 1.0L, 1.17518615722656250000E0L,
   1.50364172389568823819E-5L, 1.54308063481524377848E0L, TOL},
  {"sinh", sinhl, 7.09089565712818057364E2L, 4.49423283712885057274E307L,
   1.70878916528708958045E289L,  4.49423283712885057274E307L, TOL},
  {"sinh", sinhl, 2.22044604925031308085E-16L, 0.00000000000000000000E0L,
   2.22044604925031308085E-16L, 1.00000000000000000000E0L, TOL},
  {"sinh", sinhl, 3.7252902984619140625e-9L, 3.7252902984619140625e-9L,
   8.616464714094038285889380656847999229E-27L,
   1.00000000000000000693889L, TOL},
  {"sinh", sinhl, 2.3283064365386962890625e-10L, 2.3283064365386962890625e-10L,
   2.103629080589364814436978072135626630E-30,
   1.000000000000000000027105L, TOL},
  {"cosh", coshl, 7.09089565712818057364E2L, 4.49423283712885057274E307L,
   1.70878916528708958045E289L, 4.49423283712885057274E307L, TOL},
  {"cosh", coshl, 1.0L, 1.54307556152343750000E0L,
   5.07329180627847790562E-6L, 1.17520119364380145688E0L, TOL},
  {"cosh", coshl, 0.5L, 1.12762451171875000000E0L,
   1.45348763078522622516E-6L, 5.21095305493747361622E-1L, TOL},
  {"tanh", tanhl, 0.5L, 4.62112426757812500000E-1L,
   4.73050219725850231848E-6L, 7.86447732965927410150E-1L, TOL},
  {"tanh", tanhl, 5.49306144334054780032E-1L, 4.99984741210937500000E-1L,
   1.52587890624507506378E-5L, 7.50000000000000049249E-1L, TOL},
  {"tanh", tanhl, 0.625L, 5.54595947265625000000E-1L,
   3.77508375729399903910E-6L, 6.92419147969988069631E-1L, TOL},
  {"asinh", asinhl, 0.5L, 4.81201171875000000000E-1L,
   1.06531846034474977589E-5L, 8.94427190999915878564E-1L, TOL},
  {"asinh", asinhl, 1.0L, 8.81362915039062500000E-1L,
   1.06719804805252326093E-5L, 7.07106781186547524401E-1L, TOL},
  {"asinh", asinhl, 2.0L, 1.44363403320312500000E0L,
   1.44197568534249327674E-6L, 4.47213595499957939282E-1L, TOL},
  {"acosh", acoshl, 2.0L, 1.31695556640625000000E0L,
   2.33051856670862504635E-6L, 5.77350269189625764509E-1L, TOL},
  {"acosh", acoshl, 1.5L, 9.62417602539062500000E-1L,
   6.04758014439499551783E-6L, 8.94427190999915878564E-1L, TOL},
  {"acosh", acoshl, 1.03125L, 2.49343872070312500000E-1L,
   9.62177257298785143908E-6L, 3.96911150685467059809E0L, TOL},
  {"atanh", atanhl, 0.5L, 5.49301147460937500000E-1L,
   4.99687311734569762262E-6L, 1.33333333333333333333E0L, TOL},

#if 0
  {"j0", j0l, 8.0L, 1.71646118164062500000E-1L,
   4.68897349140609086941E-6L, -2.34636346853914624381E-1, -4},
  {"j0", j0l, 4.54541015625L, -3.09783935546875000000E-1L,
   7.07472668157686463367E-6L, 2.42993657373627558460E-1L, -4},
  {"j0", j0l, 2.85711669921875L, -2.07901000976562500000E-1L,
   1.15237285263902751582E-5L, -3.90402225324501311651E-1L, -4},
  {"j0", j0l, 2.0L, 2.23876953125000000000E-1L,
   1.38260162356680518275E-5L, -5.76724807756873387202E-1L, -4},
  {"j0", j0l, 1.16415321826934814453125e-10L, 9.99984741210937500000E-1L,
   1.52587890624999966119E-5L, 9.99999999999999999997E-1L, -4},
  {"j0", j0l, -2.0L, 2.23876953125000000000E-1L,
   1.38260162356680518275E-5L, 5.76724807756873387202E-1L, -4},
  {"y0", y0l, 8.0L, 2.23510742187500000000E-1L,
   1.07472000662205273234E-5L, 1.58060461731247494256E-1L, -4},
  {"y0", y0l, 4.54541015625L, -2.08114624023437500000E-1L,
   1.45018823856668874574E-5L, -2.88887645307401250876E-1L, -4},
  {"y0", y0l, 2.85711669921875L, 4.20303344726562500000E-1L,
   1.32781607563122276008E-5L, -2.82488638474982469213E-1, -4},
  {"y0", y0l, 2.0L, 5.10360717773437500000E-1L,
   1.49548763076195966066E-5L, 1.07032431540937546888E-1L, -4},
  {"y0", y0l, 1.16415321826934814453125e-10L, -1.46357574462890625000E1L,
   3.54110537011061127637E-6L, 5.46852220461145271913E9L, -4},
  {"j1", j1l, 8.0L, 2.34634399414062500000E-1L,
   1.94743985212438127665E-6L,1.42321263780814578043E-1, -4},
  {"j1", j1l, 4.54541015625L, -2.42996215820312500000E-1L,
   2.55844668494153980076E-6L, -2.56317734136211337012E-1, -4},
  {"j1", j1l, 2.85711669921875L, 3.90396118164062500000E-1L,
   6.10716043881165077013E-6L, -3.44531507106757980441E-1L, -4},
  {"j1", j1l, 2.0L, 5.76721191406250000000E-1L,
   3.61635062338720244824E-6L,  -6.44716247372010255494E-2L, -4},
  {"j1", j1l, 1.16415321826934814453125e-10L,
   5.820677273504770710133016109466552734375e-11L,
   8.881784197001251337312921818461805735896e-16L,
   4.99999999999999999997E-1L, -4},
  {"j1", j1l, -2.0L, -5.76721191406250000000E-1L,
   -3.61635062338720244824E-6L,  -6.44716247372010255494E-2L, -4},
  {"y1", y1l, 8.0L, -1.58065795898437500000E-1L,
   5.33416719000574444473E-6L, 2.43279047103972157309E-1L, -4},
  {"y1", y1l, 4.54541015625L, 2.88879394531250000000E-1L,
   8.25077615125087585195E-6L, -2.71656024771791736625E-1L, -4},
  {"y1", y1l, 2.85711669921875L, 2.82485961914062500000E-1,
   2.67656091996921314433E-6L, 3.21444694221532719737E-1, -4},
  {"y1", y1l, 2.0L, -1.07040405273437500000E-1L,
   7.97373249995311162923E-6L, 5.63891888420213893041E-1, -4},
  {"y1", y1l, 1.16415321826934814453125e-10L, -5.46852220500000000000E9L, 
   3.88547280871200700671E-1L, 4.69742480525120196168E19L, -4},
#endif

  {"tgamma", tgammal, 1.0L, 1.0L,
   0.0L, -5.772156649015328606e-1L, TOL},
  {"tgamma", tgammal, 2.0L, 1.0L,
   0.0L, 4.2278433509846713939e-1L, TOL},
  {"tgamma", tgammal, 3.0L, 2.0L,
   0.0L, 1.845568670196934279L, TOL},
  {"tgamma", tgammal, 4.0L, 6.0L,
   0.0L, 7.536706010590802836L, TOL},

  {"lgamma", lgammal, 8.0L, 8.525146484375L,
   1.48766904143001655310E-5, 2.01564147795560999654E0L, TOL},
  {"lgamma", lgammal, 8.99993896484375e-1L, 6.6375732421875e-2L,
    5.11505711292524166220E-6L, -7.54938684259372234258E-1, -TOL},
  {"lgamma", lgammal, 7.31597900390625e-1L, 2.2369384765625e-1L,
    5.21506341809849792422E-6L,-1.13355566660398608343E0L, -TOL},
  {"lgamma", lgammal, 2.31639862060546875e-1L, 1.3686676025390625L,
    1.12609441752996145670E-5L, -4.56670961813812679012E0, -TOL},
  {"lgamma", lgammal, 1.73162841796875L, -8.88214111328125e-2L,
    3.36207740803753034508E-6L, 2.33339034686200586920E-1L, -TOL},
  {"lgamma", lgammal, 1.23162841796875L,-9.3902587890625e-2L,
    1.28765089229009648104E-5L, -2.49677345775751390414E-1L, -TOL},
  {"lgamma", lgammal, 7.3786976294838206464e19L, 3.301798506038663053312e21L,
    -1.656137564136932662487046269677E5L, 4.57477139169563904215E1L, TOL},
  {"lgamma", lgammal, 1.0L, 0.0L,
    0.0L, -5.77215664901532860607E-1L, -TOL},
  {"lgamma", lgammal, 2.0L, 0.0L,
    0.0L, 4.22784335098467139393E-1L, -TOL},
  {"lgamma", lgammal, 1.08420217248550443401E-19L,4.36682586669921875e1L,
    1.37082843669932230418E-5L, -9.22337203685477580858E18L, TOL},
  {"lgamma", lgammal, -0.5L, 1.2655029296875L,
    9.19379714539648894580E-6L, 3.64899739785765205590E-2L, TOL},
  {"lgamma", lgammal, -1.5L,  8.6004638671875e-1L,
    6.28657731014510932682E-7L, 7.03156640645243187226E-1L, TOL},
  {"lgamma", lgammal, -2.5L,  -5.6243896484375E-2L,
    1.79986700949327405470E-7, 1.10315664064524318723E0L, -TOL},
  {"lgamma", lgammal, -3.5L,  -1.30902099609375L,
    1.43111007079536392848E-5L, 1.38887092635952890151E0L, TOL},

  {"null", NULL, 0.0L, 0.0L, 0.0L, 1},
};

/* These take care of extra-precise floating point register problems.  */
static volatile long double volat1;
static volatile long double volat2;


/* Return the next nearest floating point value to X
   in the direction of UPDOWN (+1 or -1).
   (Might fail if X is denormalized.)  */

static long double
nextval (x, updown)
     long double x;
     int updown;
{
  long double m;
  int i;

  volat1 = x;
  m = 0.25L * MACHEPL * volat1 * updown;
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
monotl ()
{
  long double (*fun1) (long double);
  int i, j, errs, tests, err_thresh;
  long double x, x0, dy, err;

  errs = 0;
  tests = 0;
  i = 0;

  for (;;)
    {
      /* Function call reference.  */
      fun1 = test1[i].func;
      if (fun1 == NULL)
	break;
      /* Function argument.  */
      volat1 = test1[i].arg1;
      /* x0 is the given argument, x scans from slightly below to above x0. */
      x0 = volat1;
      x = volat1;
      for (j = 0; j <= NPTS; j++)
	{
	  /* delta x */
	  volat1 = x - x0;
	  /* delta y */
	  dy = volat1 * test1[i].derivative;
	  /* y + delta y */
	  dy = test1[i].answer2 + dy;
	  volat1 = test1[i].answer1 + dy;
	  /* Run the function under test.  */
	  volat2 = (*(fun1)) (x);
	  if (volat2 != volat1)
	    {
	      /* Estimate difference between program result
		 and extended precision function value.  */
	      err = volat2 - test1[i].answer1;
	      err = err - dy;
	      /* Compare difference with reporting threshold.  */
	      err_thresh = test1[i].thresh;
	      if (err_thresh >= 0)
		err = err / volat1; /* relative error */
	      else
		{
		  err_thresh = -err_thresh; /* absolute error */
		  /* ...but relative error if function value > 1 */
		  if (fabsl(volat1) > 1.0L)
		    err = err / volat1;
		}
	      if (fabsl (err) > (err_thresh * MACHEPL))
		{
		  printf ("%d %s(%.19Le) = %.19Le, rel err = %.3Le\n",
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
	      err_thresh = test1[i].thresh;
	      if (err_thresh >= 0)
		err = err / volat1; /* relative error */
	      else
		{
		  err_thresh = -err_thresh;
		  if (fabsl(volat1) > 1.0L)
		    err = err / volat1;
		}
	      if (fabsl (err) > (err_thresh * MACHEPL))
		{
		  printf ("%d %s(%.19Le) = %.19Le, rel err = %.3Le\n",
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
#endif	/* LDBL_MANT_DIG == 64 */
