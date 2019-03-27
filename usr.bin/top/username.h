/*
 *  This program may be freely redistributed,
 *  but this entire comment MUST remain intact.
 *
 *  Copyright (c) 1984, 1989, William LeFebvre, Rice University
 *  Copyright (c) 1989, 1990, 1992, William LeFebvre, Northwestern University
 *  Copyright (c) 2016, Randy Westlund
 *
 * $FreeBSD$
 */
#ifndef USERNAME_H
#define USERNAME_H

#include <stdbool.h>

int	 enter_user(int uid, char *name, bool wecare);
int	 get_user(int uid);
void	 init_hash(void);
char 	*username(int uid);
int 	 userid(char *username);

/*
 *  "Table_size" defines the size of the hash tables used to map uid to
 *  username.  The number of users in /etc/passwd CANNOT be greater than
 *  this number.  If the error message "table overflow: too many users"
 *  is printed by top, then "Table_size" needs to be increased.  Things will
 *  work best if the number is a prime number that is about twice the number
 *  of lines in /etc/passwd.
 */
#define Table_size	20011

#endif /* USERNAME_H */
