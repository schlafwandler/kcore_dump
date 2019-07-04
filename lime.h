// from https://github.com/504ensicsLabs/LiME/blob/master/doc/README.md
typedef struct {
    unsigned int magic;        // Always 0x4C694D45 (LiME)
    unsigned int version;        // Header version number
    unsigned long long s_addr;    // Starting address of physical RAM range
    unsigned long long e_addr;    // Ending address of physical RAM range
    unsigned char reserved[8];    // Currently all zeros
} __attribute__ ((__packed__)) lime_mem_range_header;
