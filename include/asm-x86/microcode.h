struct microcode_header {
	unsigned int            hdrver;
	unsigned int            rev;
	unsigned int            date;
	unsigned int            sig;
	unsigned int            cksum;
	unsigned int            ldrver;
	unsigned int            pf;
	unsigned int            datasize;
	unsigned int            totalsize;
	unsigned int            reserved[3];
};

struct microcode {
	struct microcode_header hdr;
	unsigned int            bits[0];
};

/* microcode format is extended from prescott processors */
struct extended_signature {
	unsigned int            sig;
	unsigned int            pf;
	unsigned int            cksum;
};

struct extended_sigtable {
	unsigned int            count;
	unsigned int            cksum;
	unsigned int            reserved[3];
	struct extended_signature sigs[0];
};
