/* Firmware (V1.7.0) functions and globals used by the CFW blob. All in mod00 unless noted. */
#ifndef FW_H
#define FW_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define FW(addr, ret, ...) ((ret (*)(__VA_ARGS__))((addr) | 1))

/* storage: byte-addressed read (whole aligned sectors go straight to the driver), sector write */
#define fw_read        FW(0x0306b36c, int, u32 dev, u32 byteaddr, u32 len, void *buf)
#define fw_write       FW(0x0306b75e, int, u32 dev, u32 lba, u32 nsec, const void *buf)
#define DEV_DB         0x30001

#define fw_lock        FW(0x0306a40e, void, void)         /* nestable, masks the FS-using IRQs */
#define fw_unlock      FW(0x0306a466, void, void)
#define fw_ticks       FW(0x0306a532, u32, void)          /* 10 ms system ticks */
#define fw_wdt         FW(0x03065800, void, void)         /* watchdog kick */
#define fw_load_module FW(0x0306b6bc, void, u32 id, u32 flags)
#define fw_vsnprintf   FW(0x03071e48, int, char *buf, int size, const char *fmt, __builtin_va_list ap)

/* FAT file API (names are 11-char 8.3 without dot; exFAT takes "NAME.EXT") */
#define fw_file_create FW(0x0305fba6, int, const char *dir, const char *name)
#define fw_file_write  FW(0x0305fda4, u32, const void *buf, u32 offset, u32 len, int h)
#define fw_file_close  FW(0x0305c2e0, int, int h)
#define fw_file_delete FW(0x0305fbf2, int, const char *dir, const char *name)
#define FS_TYPE        (*(volatile u8 *)0x03076b24)       /* 3 = exFAT */

/* open files: handle 0..7 -> FILES + h * FILE_SIZE (unaligned u32 fields) */
#define FREAD_ADDR     0x0305c3fa                         /* u32 fread(void *dst, u32 n, u32 h) */
#define fw_fseek       FW(0x0305c8f2, int, u32 off, u32 whence, u32 h)   /* 0 set, 1 cur; 0 = ok */
#define FILES          ((u8 *)0x03077668)
#define FILE_SIZE      0x27e
#define NFILES         8
#define CLUSTER_BAD    0x0ffffff7                         /* >= : end of chain / error */
struct file {
    u8 open;
    u8 _pad[0x0f];
    u32 size;                                             /* +0x10 */
    u32 first;                                            /* +0x14 first cluster */
    u32 cur;                                              /* +0x18 cluster holding pos */
    u32 _pad1c;
    u32 pos;                                              /* +0x20 */
} __attribute__((packed));

/* tag parser: fills a song record from the file's tags; ext = upper-case "FLA", "MP3", ... */
#define fw_tags        FW(0x03071178, int, u32 h, u8 *rec, const char *ext)
/* exFAT: copy raw 32-byte directory entry `index` of the stream described by desc; 0 = ok */
#define fw_exfat_entry FW(0x0305b2a2, int, u8 *dst, const void *desc, u32 index)

/* media library */
#define DB_BASE        (*(volatile u32 *)0x0300d148)      /* DB base sector */
#define LIB            ((volatile u16 *)0x0300d164)       /* library counters, u16 index */
#define LIB_ALBUMS     (LIB[0x34 / 2])

/* refresh pipeline (mod05 / mod39 / mod40) */
#define MOD39_ID 0x27
#define MOD40_ID 0x28
#define mod05_progress FW(0x03079418, void, u32 step)     /* mod05 */
#define mod39_walk     FW(0x0307a726, void, void)         /* mod39 */
#define mod39_record   FW(0x0307b1c0, void, void)         /* mod39 RECORD folder */
#define mod39_mkrec    FW(0x0307a4d0, int, u8 *dst, u8 *src, int is_file)
#define mod40_sort     FW(0x0307a0dc, void, volatile u16 *counts, u32 base)

/* mod40 comparator state (see FUN_03079810) */
#define SORT_FIELD     (*(volatile u32 *)0x0300e6e4)      /* record offset of the key */
#define SORT_AREA      (*(volatile u32 *)0x0300e6e8)      /* sector offset of the record area (0 = songs) */
#define CMP_CACHE_A    (*(volatile u32 *)(0x0307a3d4 + 8))
#define CMP_CACHE_B    (*(volatile u32 *)(0x0307a3d4 + 12))
#define CMP_BUF_A      ((u16 *)0x0307b474)
#define CMP_BUF_B      ((u16 *)0x0307b574)

/* one level of a grouped sort: the group tables of every level, then the song
 * list (see FUN_0307a0dc). Table sectors come from the fixed array SORT_TABLES:
 * [0] song list, [1] innermost groups .. [levels-1] top-level groups. */
#define mod40_levels   FW(0x03079ed8, void, volatile u32 *fields, volatile u16 *counts, u32 levels, \
                          u32 *groups, u32 top, u32 buf_a, u32 buf_b, u32 p8, u32 p9, u32 p10)
#define SORT_TABLES    ((volatile u32 *)0x0309b674)       /* u32[4] DB sectors */
#define SORT_FIELDS    ((volatile u32 *)0x0309b684)       /* u32[4] record offsets, [levels-1] = top */
#define SORT_NODES     0x0302ac00                         /* song list buffer, 0x4000 */
#define SORT_BUF_A     0x0307b674                         /* group buffers, 0x10000 each */
#define SORT_BUF_B     0x0308b674
#define fw_memclr      FW(0x030579a8, void, u32 addr, u32 len)

/* song record (0x800 bytes at DB_BASE + recno * 4) */
#define REC_TRACK_ARTIST 0x100       /* cfw: ARTIST tag (the stock +0x500 holds the album artist) */
#define REC_DATE         0x2f8       /* cfw: album folder date */

/* DB layout, sectors relative to DB_BASE */
#define SEC_SONGS_BY_ALBUM 0x52140   /* u16 recno list, 0x20 sectors */
#define SEC_ALBUM_GROUPS   0x52160   /* 8-byte entries, 0x80 sectors */
#define SEC_RECENT_GROUPS  0x52380   /* cfw: album groups newest first, 0x80 sectors */
#define SEC_ARTIST_SONGS   0x52800   /* cfw: track Artists song list, 0x20 sectors */
#define SEC_ARTIST_GROUPS  0x52820   /* cfw: track artists, 0x80 sectors; entry 0 u16[3] = count */
#define SEC_ARTIST_ALBUMS  0x528a0   /* cfw: albums per track artist, 0x80 sectors */
#define SEC_KEYS           0x52a00   /* cfw: 6 fields x 0x80 sectors, 8 bytes per song */
#define SEC_HASHES         0x52d00   /* cfw: 6 fields x 0x40 sectors, u32 per song */
#define SEC_DATES          0x52e80   /* cfw: 0x40 sectors, u32 per song */
#define MAX_SONGS          8192

#endif
