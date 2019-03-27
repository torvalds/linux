/* @(#)msg.x	2.1 88/08/11 4.0 RPCSRC */
/*
 * msg.x: Remote message printing protocol
 */
program MESSAGEPROG {
	version MESSAGEVERS {
		int PRINTMESSAGE(string) = 1;
	} = 1;
} = 99;
