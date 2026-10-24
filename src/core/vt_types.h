/* vt_types.h - types shared by the core, the app and the platforms. No VMS types. */
#ifndef VT_TYPES_H
#define VT_TYPES_H

#define VT_VERSION "0.1.0"
#define VT_PCSI_VERSION "T0.1-0"

#define VT_SPEC_MAX 1024     /* a full file specification (RMS allows 4095) */
#define VT_NAME_MAX 256      /* one name or type field, escapes included */

/* A VMS time: 100 ns units since 17-NOV-1858 00:00, local time. 0 = none. */
typedef unsigned long long vt_time;

/* What the file system says about one file. Sizes are in 512-byte blocks. */
typedef struct {
    unsigned int status;          /* 1 = read; else the condition from the read */
    unsigned int eof_blocks;      /* blocks in use (end-of-file block, adjusted) */
    unsigned int alloc_blocks;
    unsigned short ffb;           /* first free byte in the EOF block */
    unsigned int uic;             /* group << 16 | member */
    unsigned short prot;          /* S,O,G,W nibbles; a set bit denies R,W,E,D */
    unsigned char rfm, org, rat;  /* record format, organisation, record attributes */
    unsigned short mrs;           /* maximum record size */
    unsigned short lrl;           /* longest record */
    unsigned char vfc;            /* VFC header size */
    unsigned short defext;        /* default extend quantity */
    unsigned short versions;      /* a directory's default version limit (0 = none) */
    unsigned int uchar;           /* file characteristics (FCH$) */
    unsigned short revisions;
    vt_time cdt, rdt, edt, bdt;   /* created, revised, expires, backed up */
} vt_attrs;

/* Record formats and organisations, as stored in the file header. */
enum { VT_RFM_UDF, VT_RFM_FIX, VT_RFM_VAR, VT_RFM_VFC, VT_RFM_STM, VT_RFM_STMLF,
       VT_RFM_STMCR };
enum { VT_ORG_SEQ = 0, VT_ORG_REL = 1, VT_ORG_IDX = 2 };

/* Keys. Printable characters and control characters are their own codes. */
enum {
    VK_NONE = -1, VK_TIMEOUT = -2,
    VK_CTRL_A = 1, VK_CTRL_E = 5, VK_CTRL_H = 8, VK_TAB = 9, VK_RETURN = 13,
    VK_CTRL_U = 21, VK_CTRL_W = 23, VK_CTRL_Z = 26, VK_DEL = 127,
    VK_UP = 256, VK_DOWN, VK_LEFT, VK_RIGHT,
    VK_PF1, VK_PF2, VK_PF3, VK_PF4,
    VK_FIND, VK_INSERT, VK_REMOVE, VK_SELECT, VK_PREV, VK_NEXT,
    VK_HELP, VK_DO, VK_F10, VK_ENTER,
    VK_KP0, VK_KP1, VK_KP2, VK_KP3, VK_KP4, VK_KP5, VK_KP6, VK_KP7, VK_KP8, VK_KP9,
    VK_KP_MINUS, VK_KP_COMMA, VK_KP_PERIOD,
    VK_OTHER
};

#endif
