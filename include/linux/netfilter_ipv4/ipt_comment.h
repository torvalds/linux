#ifndef _IPT_COMMENT_H
#define _IPT_COMMENT_H

#define IPT_MAX_COMMENT_LEN 256

struct ipt_comment_info {
	unsigned char comment[IPT_MAX_COMMENT_LEN];
};

#endif /* _IPT_COMMENT_H */
