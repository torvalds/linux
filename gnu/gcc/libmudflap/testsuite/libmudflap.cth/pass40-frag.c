#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static void *
func (void *p)  
{
        return (NULL);
}               

static void
test (void)
{
        int             rc;
        pthread_attr_t  my_pthread_attr;
        pthread_t       h;
        long            i;

        rc = pthread_attr_init (&my_pthread_attr);

        for (i = 1; i <= 10000; ++i) {
                if (i%100 == 0) fprintf (stderr, "%i ", i);
                if (i%1000 == 0) fprintf (stderr, "\n");
#ifndef STATIC 
	  /* Some glibc versions don't like static multithreaded programs doing this. */
                if (i==5000) __mf_set_options ("-thread-stack=192");
#endif
                rc = pthread_create (&h, &my_pthread_attr,
                        func, NULL);
                if (rc)
                        break;

                rc = pthread_join (h, NULL);
                if (rc)
                        break;
        }
        
        rc = pthread_attr_destroy (&my_pthread_attr);
}
                
int main ()
{
        test ();
                
        return (0);
}

/* { dg-timeout 20 } */
/* { dg-output "100 200 300 400 500 600 700 800 900 1000 \n" } */
/* { dg-output "1100 1200 1300 1400 1500 1600 1700 1800 1900 2000 \n" } */
/* { dg-output "2100 2200 2300 2400 2500 2600 2700 2800 2900 3000 \n" } */
/* { dg-output "3100 3200 3300 3400 3500 3600 3700 3800 3900 4000 \n" } */
/* { dg-output "4100 4200 4300 4400 4500 4600 4700 4800 4900 5000 \n" } */
/* { dg-output "5100 5200 5300 5400 5500 5600 5700 5800 5900 6000 \n" } */
/* { dg-output "6100 6200 6300 6400 6500 6600 6700 6800 6900 7000 \n" } */
/* { dg-output "7100 7200 7300 7400 7500 7600 7700 7800 7900 8000 \n" } */
/* { dg-output "8100 8200 8300 8400 8500 8600 8700 8800 8900 9000 \n" } */
/* { dg-output "9100 9200 9300 9400 9500 9600 9700 9800 9900 10000 \n" } */

