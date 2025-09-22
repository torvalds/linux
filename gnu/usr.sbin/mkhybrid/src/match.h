/*
 * 27th March 1996. Added by Jan-Piet Mens for matching regular expressions
 * 		    in paths.
 * 
 */

/*
 * 	$Id: match.h,v 1.5 2008/04/18 20:52:34 millert Exp $
 */

#include <inttypes.h>

int matches	__PR((char *fn));

int i_matches	__PR((char *fn));
intptr_t i_ishidden	__PR((void));

int j_matches	__PR((char *fn));
intptr_t j_ishidden	__PR((void));

int add_match	__PR((char *fn));
int i_add_match __PR((char *fn));
int j_add_match __PR((char *fn));

#ifdef APPLE_HYB
int hfs_add_match __PR((char *fn));
void hfs_add_list __PR((char *fn));
int hfs_matches __PR((char *fn));
intptr_t hfs_ishidden __PR((void));

void add_list __PR((char *fn));
void i_add_list __PR((char *fn));
void j_add_list __PR((char *fn));
#endif /* APPLE_HYB */
