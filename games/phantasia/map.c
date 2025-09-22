/*	$OpenBSD: map.c,v 1.4 2016/01/06 09:29:34 tb Exp $	*/
/*	$NetBSD: map.c,v 1.2 1995/03/24 03:58:58 cgd Exp $	*/

#define	minusminus	plusplus
#define	minusplus	plusminus

int
main(int argc, char *argv[])
{
    /* Set up */

    openpl();
    space(-1400, -1000, 1200, 1200);

    /* Big box */

    move(-1400, -1000);
    cont(-1400, 1000);
    cont(600, 1000);
    cont(600, -1000);
    cont(-1400, -1000);

    /* Grid -- horizontal lines every 200 */

    linemod("dotted");
    line(600, -800, -1400, -800);
    line(-1400, -600, 600, -600);
    line(600, -400, -1400, -400);
    line(-1400, -200, 600, -200);
    linemod("solid");
    line(600, 0, -1400, 0);
    linemod("dotted");
    line(-1400, 200, 600, 200);
    line(600, 400, -1400, 400);
    line(-1400, 600, 600, 600);
    line(600, 800, -1400, 800);

    /* Grid -- vertical lines every 200 */

    line(-1200, 1000, -1200, -1000);
    line(-1000, 1000, -1000, -1000);
    line(-800, 1000, -800, -1000);
    line(-600, 1000, -600, -1000);
    linemod("solid");
    line(-400, 1000, -400, -1000);
    linemod("dotted");
    line(-200, 1000, -200, -1000);
    line(0, 1000, 0, -1000);
    line(200, 1000, 200, -1000);
    line(400, 1000, 400, -1000);

    /* Circles radius +250 on "center" */

    linemod("solid");
    circle(-400, 0, 250);
    circle(-400, 0, 500);
    circle(-400, 0, 750);
    circle(-400, 0, 1000);

    /* A few labels */

    move(-670, 1075);
    label("- THE PHANTASIA UNIVERSE -");
    line(-630, 1045, -115, 1045);
    move(-360, 80);
    label("Lorien");
    move(-385, -100);
    label("Ithilien");
    move(-560, 80);
    label("Rohan");
    move(-580, -100);
    label("Anorien");
    plusplus("Rovanion", -250, 320);
    plusplus("The Iron Hills", -100, 560);
    plusplus("Rhun", 250, 570);
    minusplus("Dunland", -700, 160);
    minusplus("Eriador", -920, 300);
    minusplus("The Northern Waste", -1240, 320);
    minusminus("Gondor", -720, -180);
    minusminus("South Gondor", -940, -270);
    minusminus("Far Harad", -1100, -500);
    plusminus("Mordor", -180, -300);
    plusminus("Khand", 0, -500);
    plusminus("Near Harad", 40, -780);
    move(340, 900);
    label("The Moors");
    move(300, 840);
    label("Adventurous");
    move(340, -840);
    label("The Moors");
    move(300, -900);
    label("Adventurous");
    move(-1340, 900);
    label("The Moors");
    move(-1340, 840);
    label("Adventurous");
    move(-1340, -840);
    label("The Moors");
    move(-1340, -900);
    label("Adventurous");
    move(700, 1000);
    label("OUTER CIRCLES:");
    line(690, 970, 1000, 970);
    move(700, 900);
    label("> 9:  The Outer Waste");
    move(700, 800);
    label("> 20: The Dead Marshes");
    move(700, 700);
    label("> 35: Kennaquhair");
    move(700, 600);
    label("> 55: Morannon");
    move(700, 300);
    label("(0,0): The Lord's Chamber");

    move(700, -400);
    label("Grid squares are 100 x 100");
    move(700, -800);
    label("Created by Ted Estes");
    move(700, -860);
    label("Plotted by Chris Robertson");
    move(700, -920);
    label(" c  1985");
    circle(723, -923, 20);

    /* Close down */

    move(-1380, 1180);
    closepl();
    return 0;
}

/* draw strings in plus plus quadrant */
void
plusplus(char *s, int x, int y)
{
char	s1[2];

    while (*s)
	{
	move(x, y);
	s1[0] = *s++;
	s1[1] = '\0';
	label(s1);
	x += 25;
	y -= 30;
	}
}

/* draw strings in plus minus quadrant */
void
plusminus(char *s, int x, int y)
{
char	s1[2];

    while (*s)
	{
	move(x, y);
	s1[0] = *s++;
	s1[1] = '\0';
	label(s1);
	x += 25;
	y += 30;
	}
}
