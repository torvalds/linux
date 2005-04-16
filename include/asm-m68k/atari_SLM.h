
#ifndef _ATARI_SLM_H
#define _ATARI_SLM_H

/* Atari SLM laser printer specific ioctls */

#define	SLMIOGSTAT	0xa100
#define	SLMIOGPSIZE	0xa101
#define	SLMIOGMFEED	0xa102

#define	SLMIORESET	0xa140

#define	SLMIOSPSIZE	0xa181
#define	SLMIOSMFEED	0xa182

/* Status returning structure (SLMIOGSTAT) */
struct SLM_status {
	int		stat;		/* numeric status code */
	char	str[40];	/* status string */
};

/* Paper size structure (SLMIO[GS]PSIZE) */
struct SLM_paper_size {
	int		width;
	int		height;
};

#endif /* _ATARI_SLM_H */
