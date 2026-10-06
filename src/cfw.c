/*
 * Media-library refresh extensions, run from RAM during the refresh:
 *
 *  - Recent Albums: every folder's FAT creation date is captured while the
 *    walker (mod39) lists it, stamped into each song record (+0x2f8), and after
 *    the stock sort a copy of the album group table is written in
 *    newest-folder-first order to SEC_RECENT_GROUPS.
 *
 *  - Faster sort: for each song the walker also writes a compact copy of the
 *    sort keys (first 4 chars of title/artist/album/genre/track key) and a
 *    hash of each full key string. mod40's key loader reads the compact copy
 *    in multi-sector chunks instead of one sector per song, and its comparator
 *    settles 4-char ties by hash instead of re-reading both records. Order is
 *    unchanged; only a 32-bit hash collision between two different strings
 *    with the same 4-char prefix could merge them.
 *
 *  - Track Artists: the stock walker overwrites a song's artist (+0x500) with
 *    its album artist, so the stock Artists list groups by album artist. The
 *    ARTIST tag is kept in +0x100 and an extra sort pass builds a second
 *    artist -> album -> song hierarchy over it (SEC_ARTIST_*), which the
 *    browser shows as "Artists" while the stock list becomes "Album Artists".
 *
 *  - Faster tag reading: the stock tag parsers read FLAC comment names one byte
 *    per call, and every read below a sector costs a whole SD sector read. For
 *    the walk, the stock file read is redirected to cfw_fread, which serves
 *    small reads from a 4 KB window of the file.
 *
 *  - CFWLOG.TXT in the root of the scanned volume: timings and counters.
 */
#include <stdarg.h>
#include "fw.h"
#include "prof.h"

#define FOLDER_TAB ((struct folder *)0x03092400)  /* only mod40 maps here; free during the walk */
#define FOLDER_MAX 3584
#define WIN_BUF    ((u8 *)(FOLDER_TAB + FOLDER_MAX)) /* 0x03099400, read window, same reason */
#define WIN_SIZE   0x1000
#define LOG_FOLDERS 12
#define LOG_ALBUMS 12

struct folder { u32 cluster, date; };

/* sort fields in compact-store order */
#define NFIELDS 6
static const u16 field_off[NFIELDS] = { 0x14, 0x400, 0x500, 0x600, 0x700, REC_TRACK_ARTIST };

static int field_index(u32 off)
{
    for (int i = 0; i < NFIELDS; i++)
        if (field_off[i] == off)
            return i;
    return -1;
}

static struct {
    u32 base;
    u32 t_start, t_walk, t_record, t_sort, t_artists, t_recent;
    u32 songs, seq_err, folders, folders_dated, folders_dropped, songs_dated;
    u32 key_hits, key_miss, key_loads;
    u32 cmp_calls, cmp_hash_eq, cmp_full, cmp_reads, hash_loads;
    u32 albums, artists, write_err;
    u32 us_hooks, us_keyload, us_cmpread, us_hashload;
    u32 rd_hits, rd_fills, rd_big, rd_seeks, us_fill;
} st;

#define KWIN_SEC 4      /* key window: 4 sectors = 256 songs of one field */
#define KWINS 2
#define HWINS 8         /* hash windows: 1 sector = 128 songs of one field */

static union {
    struct {                                    /* walk: one sector per stream */
        u8 keys[NFIELDS][512];
        u32 hashes[NFIELDS][128];
        u32 dates[128];
        int ksec, hsec;                         /* buffered sector index, -1 = none */
    } w;
    struct {                                    /* sort: read caches */
        u8 kbuf[KWINS][KWIN_SEC * 512];
        u32 hbuf[HWINS][128];
        u32 ktag[KWINS], htag[HWINS];
        u32 knext, hnext;
    } c;
    u8 out[4096];                               /* recent index output chunk */
} buf __attribute__((aligned(4)));

/* ---- log ---------------------------------------------------------------- */

static char logbuf[4096] __attribute__((aligned(4)));
static u32 loglen;

static void log_printf(const char *fmt, ...)
{
    u32 room = sizeof(logbuf) - loglen;
    if (room < 2)
        return;
    va_list ap;
    va_start(ap, fmt);
    int n = fw_vsnprintf(logbuf + loglen, room, fmt, ap);
    va_end(ap);
    if (n > 0)
        loglen += (u32)n < room - 1 ? (u32)n : room - 1;
}

static void log_date(u32 d)
{
    u32 date = d >> 16, time = d & 0xffff;
    log_printf("%04u-%02u-%02u %02u:%02u", 1980 + (date >> 9), (date >> 5) & 15, date & 31,
         time >> 11, (time >> 5) & 63);
}

static void log_utf16(const u16 *s, u32 max)
{
    char a[33];
    u32 i = 0;
    for (; i < max && i < 32 && s[i]; i++)
        a[i] = s[i] >= 0x20 && s[i] < 0x7f ? (char)s[i] : '?';
    a[i] = 0;
    log_printf("%s", a);
}

static void write_log(void)
{
    const char *name = FS_TYPE == 3 ? "CFWLOG.TXT" : "CFWLOG  TXT";
    fw_file_delete("\\", name);
    int h = fw_file_create("\\", name);
    if (h < 0)
        return;
    fw_wdt();
    fw_file_write(logbuf, 0, loglen, h);
    fw_file_close(h);
}

/* ---- storage helpers ---------------------------------------------------- */

static void read_secs(u32 sec, u32 n, void *dst)
{
    u8 *p = dst;
    while (n) {
        u32 k = n > 16 ? 16 : n;
        fw_wdt();
        fw_read(DEV_DB, sec * 0x200, k * 0x200, p);
        sec += k, n -= k, p += k * 0x200;
    }
}

static void write_secs(u32 sec, u32 n, const void *src)
{
    if (fw_write(DEV_DB, sec, n, src))
        st.write_err++;
}

static u16 rd16(const u8 *p) { return p[0] | p[1] << 8; }

/* FNV-1a over a UTF-16 key, same extent the stock comparator compares */
static u32 key_hash(const u16 *s)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < 0x80 && s[i]; i++) {
        h = (h ^ (s[i] & 0xff)) * 16777619u;
        h = (h ^ (s[i] >> 8)) * 16777619u;
    }
    return h;
}

/* ---- walk (mod39) --------------------------------------------------------- */

static u32 folder_date(u32 cluster)
{
    static u32 last_cluster = ~0u, last_date;
    if (cluster == last_cluster)
        return last_date;
    last_cluster = cluster;
    last_date = 0;
    for (u32 i = st.folders; i--;)
        if (FOLDER_TAB[i].cluster == cluster) {
            last_date = FOLDER_TAB[i].date;
            break;
        }
    return last_date;
}

static u32 rd32(const u8 *p) { return rd16(p) | (u32)rd16(p + 2) << 16; }

/*
 * Creation and last-write timestamps (FAT date << 16 | time) of the folder the
 * walker just found. sp = walker's sp: find-next context at sp+0x56c (+4 = next
 * entry index), exFAT stream descriptor at sp+0x588, 32-byte entry at sp+0x5b8.
 * On FAT that is the real short entry. On exFAT it is the raw File (0x85) entry
 * with the generated 8.3 name written over its CreateTimestamp, so re-read it.
 */
static void folder_times(const u8 *sp, u32 *created, u32 *modified)
{
    const u8 *e = sp + 0x5b8;
    *created = *modified = 0;
    if (FS_TYPE == 3) {
        u8 raw[32] __attribute__((aligned(4)));
        if (!fw_exfat_entry(raw, sp + 0x588, rd32(sp + 0x570) - 1) && raw[0] == 0x85) {
            *created = rd32(raw + 8);
            *modified = rd32(raw + 12);
        }
    } else {
        *created = (u32)rd16(e + 0x10) << 16 | rd16(e + 0x0e);
        *modified = (u32)rd16(e + 0x18) << 16 | rd16(e + 0x16);
    }
}

int cfw_dir_record(u8 *dst, u8 *src, int is_file, const u8 *sp)
{
    int r = mod39_mkrec(dst, src, is_file);
    const u8 *e = sp + 0x5b8;
    u32 cluster = (u32)rd16(e + 0x14) << 16 | rd16(e + 0x1a);
    u32 created, modified;
    folder_times(sp, &created, &modified);
    u32 date = created >> 16 ? created : modified;
    if (date)
        st.folders_dated++;
    if (st.folders < LOG_FOLDERS) {
        log_printf("dir ");
        log_utf16((const u16 *)src, 24);         /* long name (mkrec source +0) */
        log_printf(" cl=%x created ", cluster);
        log_date(created);
        log_printf(" modified ");
        log_date(modified);
        log_printf("\r\n");
    }
    if (st.folders < FOLDER_MAX) {
        FOLDER_TAB[st.folders].cluster = cluster;
        FOLDER_TAB[st.folders].date = date;
        st.folders++;
    } else {
        st.folders_dropped++;
    }
    return r;
}

static void compact_flush(void)
{
    u32 b = st.base;
    if (buf.w.ksec >= 0)
        for (int f = 0; f < NFIELDS; f++)
            write_secs(b + SEC_KEYS + f * 0x80 + buf.w.ksec, 1, buf.w.keys[f]);
    if (buf.w.hsec >= 0) {
        for (int f = 0; f < NFIELDS; f++)
            write_secs(b + SEC_HASHES + f * 0x40 + buf.w.hsec, 1, buf.w.hashes[f]);
        write_secs(b + SEC_DATES + buf.w.hsec, 1, buf.w.dates);
    }
}

static void compact_put(u32 recno, const u8 *rec, u32 date)
{
    int ks = recno >> 6, hs = recno >> 7;
    if (ks != buf.w.ksec || hs != buf.w.hsec) {
        compact_flush();
        if (ks != buf.w.ksec) {
            __builtin_memset(buf.w.keys, 0, sizeof buf.w.keys);
            buf.w.ksec = ks;
        }
        if (hs != buf.w.hsec) {
            __builtin_memset(buf.w.hashes, 0, sizeof buf.w.hashes);
            __builtin_memset(buf.w.dates, 0, sizeof buf.w.dates);
            buf.w.hsec = hs;
        }
    }
    for (int f = 0; f < NFIELDS; f++) {
        __builtin_memcpy(buf.w.keys[f] + (recno & 63) * 8, rec + field_off[f], 8);
        buf.w.hashes[f][recno & 127] = key_hash((const u16 *)(rec + field_off[f]));
    }
    buf.w.dates[recno & 127] = date;
}

/* ARTIST of the file whose tags were just read (cfw_tags), for the next song record */
static u16 track_artist[0x80];
static int track_artist_set;

/* recno = walker's song counter (r10) */
int cfw_file_record(u8 *dst, u8 *src, int is_file, u32 recno)
{
    int r = mod39_mkrec(dst, src, is_file);
    /* cue tracks and untagged files: same as the stock artist */
    __builtin_memcpy(dst + REC_TRACK_ARTIST, track_artist_set && track_artist[0] ? track_artist : (u16 *)(dst + 0x500), 0x100);
    track_artist_set = 0;
    recno &= 0xffff;
    if (recno >= MAX_SONGS)
        return r;
    u32 t = now_us();
    u32 date = folder_date(*(u32 *)dst);    /* record +0: cluster of the song's folder */
    *(u32 *)(dst + REC_DATE) = date;
    if (date)
        st.songs_dated++;
    if (recno != st.songs)
        st.seq_err++;                       /* compact store needs consecutive recnos */
    st.songs = recno + 1;
    compact_put(recno, dst, date);
    st.us_hooks += now_us() - t;
    return r;
}

/* ---- tag reading (walk) -------------------------------------------------- */

static struct { u32 first, size, start, len; } win;     /* WIN_BUF holds [start, start + len) */
static u16 fread_saved[2];

u32 fread_orig(void *dst, u32 n, u32 h);                /* start.S */

static int in_win(const struct file *f, u32 pos, u32 n)
{
    return win.len && win.first == f->first && win.size == f->size &&
           pos >= win.start && pos + n <= win.start + win.len;
}

/*
 * Same contract as the stock read: copy up to n bytes from the handle's
 * position, advance it, return the count. Positions only move through the stock
 * seek, except within one sector, which never changes the current cluster.
 */
u32 cfw_fread(u8 *dst, u32 n, u32 h)
{
    if (h >= NFILES || n > WIN_SIZE / 2) {
        st.rd_big++;
        return fread_orig(dst, n, h);
    }
    fw_lock();
    struct file *f = (struct file *)(FILES + h * FILE_SIZE);
    u32 r;
    if (!f->open || f->cur >= CLUSTER_BAD) {
        r = fread_orig(dst, n, h);
        goto out;
    }
    u32 pos = f->pos;
    if (n > f->size - pos)
        n = f->size - pos;              /* pos <= size: the stock seek clamps it */
    if (!n) {
        r = 0;
        goto out;
    }
    if (!in_win(f, pos, n)) {
        u32 t = now_us();
        win.len = 0;
        u32 start = pos & ~0x1ffu;
        if (!fw_fseek(start, 0, h)) {
            u32 got = fread_orig(WIN_BUF, WIN_SIZE, h);
            win.first = f->first;
            win.size = f->size;
            win.start = start;
            win.len = got <= WIN_SIZE ? got : 0;
        }
        st.rd_fills++;
        st.us_fill += now_us() - t;
        if (!in_win(f, pos, n)) {
            fw_fseek(pos, 0, h);
            r = fread_orig(dst, n, h);
            goto out;
        }
    }
    __builtin_memcpy(dst, WIN_BUF + (pos - win.start), n);
    u32 end = pos + n;
    if (f->pos >> 9 == end >> 9) {
        f->pos = end;
    } else {
        fw_fseek(end, 0, h);
        st.rd_seeks++;
    }
    st.rd_hits++;
    r = n;
out:
    fw_unlock();
    return r;
}

/* point the stock read's first instruction at cfw_fread (b.w), or restore it */
static void fread_hook(int on)
{
    volatile u16 *p = (volatile u16 *)FREAD_ADDR;
    u16 hw1, hw2;
    if (on) {
        fread_saved[0] = p[0];
        fread_saved[1] = p[1];
        u32 off = ((u32)cfw_fread & ~1u) - (FREAD_ADDR + 4);
        u32 s = off >> 24 & 1;
        hw1 = 0xf000 | s << 10 | (off >> 12 & 0x3ff);
        hw2 = 0x9000 | (~(off >> 23 ^ s) & 1) << 13 | (~(off >> 22 ^ s) & 1) << 11 | (off >> 1 & 0x7ff);
    } else {
        hw1 = fread_saved[0];
        hw2 = fread_saved[1];
    }
    u32 primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask));
    p[0] = hw1;
    p[1] = hw2;
    __asm volatile("dsb\n isb\n msr primask, %0" : : "r"(primask) : "memory");
    win.len = 0;
}

/* per-format tag parse times */
static const char tag_ext[][4] = { "FLA", "MP3", "M4A", "WAV", "oth" };
#define TAG_KINDS 5                     /* last = anything else */
static u32 tag_calls[TAG_KINDS], tag_us[TAG_KINDS], tag_max[TAG_KINDS];

/* mod39 0x0307af66: bl fw_tags(h, rec, ext). rec is the walker's tag buffer;
 * after we return the walker copies its album artist (+0x458) over the artist. */
int cfw_tags(u32 h, u8 *rec, const char *ext)
{
    u32 t = now_us();
    int r = fw_tags(h, rec, ext);
    t = now_us() - t;
    __builtin_memcpy(track_artist, rec + 0x15e, 0xfe);
    track_artist[0x7f] = 0;
    track_artist_set = 1;
    u32 k = 0;
    while (k < TAG_KINDS - 1 &&
           !(ext[0] == tag_ext[k][0] && ext[1] == tag_ext[k][1] && ext[2] == tag_ext[k][2]))
        k++;
    tag_calls[k]++;
    tag_us[k] += t;
    if (t > tag_max[k])
        tag_max[k] = t;
    return r;
}

static void tags_report(void)
{
    log_printf("tags:");
    for (u32 k = 0; k < TAG_KINDS; k++)
        if (tag_calls[k])
            log_printf(" %s %u/%u ms (max %u)", tag_ext[k], tag_calls[k], tag_us[k] / 1000,
                       tag_max[k] / 1000);
    log_printf("\r\nreads: %u cached, %u fills (%u ms), %u big, %u seeks\r\n",
               st.rd_hits, st.rd_fills, st.us_fill / 1000, st.rd_big, st.rd_seeks);
}

/* ---- sort (mod40) --------------------------------------------------------- */

static int compact_ok(void) { return st.songs && !st.seq_err && !st.write_err; }

/* replaces mod40's 8-byte key reads (fw_read of record + field) */
int cfw_keyread(u32 dev, u32 addr, u32 len, void *out)
{
    if (len == 8 && SORT_AREA == 0 && compact_ok()) {
        u32 rel = addr - st.base * 0x200;
        u32 recno = rel >> 11;
        int f = field_index(rel & 0x7ff);
        if (f >= 0 && recno < st.songs) {
            u32 tag = (u32)f << 16 | recno >> 8;
            int w = 0;
            while (w < KWINS && buf.c.ktag[w] != tag)
                w++;
            if (w == KWINS) {
                w = buf.c.knext++ % KWINS;
                u32 t = now_us();
                read_secs(st.base + SEC_KEYS + f * 0x80 + (recno >> 8) * KWIN_SEC, KWIN_SEC, buf.c.kbuf[w]);
                buf.c.ktag[w] = tag;
                st.key_loads++;
                st.us_keyload += now_us() - t;
            }
            __builtin_memcpy(out, buf.c.kbuf[w] + (recno & 255) * 8, 8);
            st.key_hits++;
            return 0;
        }
    }
    st.key_miss++;
    return fw_read(dev, addr, len, out);
}

static u32 hash_of(int f, u32 recno)
{
    u32 tag = (u32)f << 16 | recno >> 7;
    int w = 0;
    while (w < HWINS && buf.c.htag[w] != tag)
        w++;
    if (w == HWINS) {
        w = buf.c.hnext++ % HWINS;
        u32 t = now_us();
        read_secs(st.base + SEC_HASHES + f * 0x40 + (recno >> 7), 1, buf.c.hbuf[w]);
        buf.c.htag[w] = tag;
        st.hash_loads++;
        st.us_hashload += now_us() - t;
    }
    return buf.c.hbuf[w][recno & 127];
}

static void load_cmp(u32 addr, volatile u32 *tag, u16 *dst, volatile u32 *other_tag, const u16 *other)
{
    if (addr == *tag)
        return;
    if (addr == *other_tag)
        __builtin_memcpy(dst, other, 0x100);
    else {
        u32 t = now_us();
        fw_read(DEV_DB, addr, 0x100, dst);
        st.cmp_reads++;
        st.us_cmpread += now_us() - t;
    }
    *tag = addr;
}

/* replaces mod40 FUN_03079810: node keys {4 upper-cased chars, recno}; 2 = a > b, 0 = a < b, 1 = equal */
u32 cfw_cmp(const u16 *a, const u16 *b)
{
    st.cmp_calls++;
    for (int i = 0; i < 4; i++) {
        if (b[i] < a[i])
            return 2;
        if (b[i] > a[i])
            return 0;
    }
    if (a[3] == 0)
        return 1;

    if (SORT_AREA == 0 && compact_ok()) {
        int f = field_index(SORT_FIELD);
        if (f >= 0 && a[4] < st.songs && b[4] < st.songs && hash_of(f, a[4]) == hash_of(f, b[4])) {
            st.cmp_hash_eq++;
            return 1;
        }
    }

    st.cmp_full++;
    u32 base = (DB_BASE + SORT_AREA) * 0x200 + SORT_FIELD;
    load_cmp(base + a[4] * 0x800, &CMP_CACHE_A, CMP_BUF_A, &CMP_CACHE_B, CMP_BUF_B);
    load_cmp(base + b[4] * 0x800, &CMP_CACHE_B, CMP_BUF_B, &CMP_CACHE_A, CMP_BUF_A);
    for (u32 i = 0; i < 0x80; i++) {
        u16 ca = CMP_BUF_A[i], cb = CMP_BUF_B[i];
        if (ca > cb)
            return 2;
        if (ca < cb)
            return 0;
        if (!ca)
            break;
    }
    return 1;
}

/* ---- recent albums ------------------------------------------------------ */

#define GROUPS ((u16 *)0x0308b674)   /* mod40 sort buffers, free once the sort is done */
#define SONGS  ((u16 *)0x0302ac00)
#define DATES  ((u32 *)0x0300ec00)
#define KEYS   ((u64 *)0x0307b674)

static void shellsort(u64 *k, u32 n)
{
    static const u16 gaps[] = { 1750, 701, 301, 132, 57, 23, 10, 4, 1 };
    for (u32 g = 0; g < sizeof gaps / sizeof gaps[0]; g++) {
        u32 gap = gaps[g];
        for (u32 i = gap; i < n; i++) {
            u64 v = k[i];
            u32 j = i;
            for (; j >= gap && k[j - gap] > v; j -= gap)
                k[j] = k[j - gap];
            k[j] = v;
        }
        fw_wdt();
    }
}

static u32 song_date(u32 recno)
{
    if (compact_ok())
        return recno < st.songs ? DATES[recno] : 0;
    u8 sec[512] __attribute__((aligned(4)));
    read_secs(st.base + recno * 4 + 1, 1, sec);   /* record +0x200..0x3ff */
    return *(u32 *)(sec + REC_DATE - 0x200);
}

static void build_recent(void)
{
    u32 n = LIB_ALBUMS, b = st.base;
    st.albums = n;
    if (!n || n > MAX_SONGS)
        return;
    read_secs(b + SEC_ALBUM_GROUPS, (n * 8 + 511) / 512, GROUPS);
    read_secs(b + SEC_SONGS_BY_ALBUM, 0x20, SONGS);
    if (compact_ok())
        read_secs(b + SEC_DATES, (st.songs * 4 + 511) / 512, DATES);

    for (u32 i = 0; i < n; i++) {
        u32 date = song_date(SONGS[GROUPS[i * 4]]);       /* first song of the album */
        KEYS[i] = (u64)(0xffffffffu - date) << 16 | i;    /* newest first, ties alphabetical */
    }
    shellsort(KEYS, n);

    u32 per = sizeof buf.out / 8, sec = b + SEC_RECENT_GROUPS;
    for (u32 j = 0; j < n; j += per) {
        u32 m = n - j < per ? n - j : per;
        __builtin_memset(buf.out, 0, sizeof buf.out);
        for (u32 k = 0; k < m; k++)
            __builtin_memcpy(buf.out + k * 8, GROUPS + (u32)(KEYS[j + k] & 0xffff) * 4, 8);
        write_secs(sec, (m * 8 + 511) / 512, buf.out);
        sec += sizeof buf.out / 512;
    }

    for (u32 j = 0; j < n && j < LOG_ALBUMS; j++) {
        u32 i = KEYS[j] & 0xffff, recno = SONGS[GROUPS[i * 4]];
        u16 name[32] __attribute__((aligned(4)));
        fw_read(DEV_DB, (b + recno * 4) * 0x200 + 0x600, sizeof name, name);
        log_printf("recent %u: ", j);
        log_date(0xffffffffu - (u32)(KEYS[j] >> 16));
        log_printf(" ");
        log_utf16(name, 32);
        log_printf("\r\n");
    }
}

/* ---- track artists ------------------------------------------------------ */

/* Same as the stock Artists pass (FUN_0307a0dc pass 2), over the ARTIST tag. */
static void build_artists(volatile u16 *counts)
{
    u32 b = st.base, n = 0;
    if (!counts[0])
        return;                         /* no songs: the stock sort skips too */
    SORT_FIELDS[0] = 0x14;
    SORT_FIELDS[1] = 0x600;
    SORT_FIELDS[2] = REC_TRACK_ARTIST;
    SORT_TABLES[0] = b + SEC_ARTIST_SONGS;
    SORT_TABLES[1] = b + SEC_ARTIST_ALBUMS;
    SORT_TABLES[2] = b + SEC_ARTIST_GROUPS;
    fw_memclr(SORT_NODES, 0x4000);
    fw_memclr(SORT_BUF_B, 0x10000);
    fw_memclr(SORT_BUF_A, 0x10000);
    mod40_levels(SORT_FIELDS, counts, 3, &n, 1, SORT_BUF_A, SORT_BUF_B, 0, 0, 0);
    st.artists = n;

    /* the browser reads the top-level count from entry 0, u16[3] (unused by stock) */
    u16 *sec = (u16 *)buf.out;
    read_secs(b + SEC_ARTIST_GROUPS, 1, sec);
    sec[3] = n;
    write_secs(b + SEC_ARTIST_GROUPS, 1, sec);
}

/* ---- driver --------------------------------------------------------------- */

#define MS(a, b) (((b) - (a)) * 10)

/* called from the mod05 refresh driver in place of: load mod39, walk, RECORD pass,
 * progress(1), load mod40, sort. lib = 0x0300d164, base = DB base sector. */
void cfw_scan(volatile u16 *lib, u32 base)
{
    st.base = base;
    buf.w.ksec = buf.w.hsec = -1;
    st.t_start = fw_ticks();
    log_printf("cfw 6.0.4 media library refresh, db base %x, fs %u\r\n", base, FS_TYPE);

    fw_load_module(MOD39_ID, 7);
    fread_hook(1);
    mod39_walk();
    fread_hook(0);
    compact_flush();
    st.t_walk = fw_ticks();
    mod39_record();
    st.t_record = fw_ticks();
    mod05_progress(1);

    __builtin_memset(&buf, 0, sizeof buf);      /* walk buffers -> sort caches */
    for (int i = 0; i < KWINS; i++)
        buf.c.ktag[i] = ~0u;
    for (int i = 0; i < HWINS; i++)
        buf.c.htag[i] = ~0u;
    fw_load_module(MOD40_ID, 7);
    mod40_sort((volatile u16 *)((u32)lib + 0x2e), base);
    st.t_sort = fw_ticks();

    build_artists((volatile u16 *)((u32)lib + 0x2e));
    st.t_artists = fw_ticks();

    build_recent();
    st.t_recent = fw_ticks();

    log_printf("walk %u ms, record %u ms, sort %u ms, artists %u ms, recent %u ms, total %u ms\r\n",
         MS(st.t_start, st.t_walk), MS(st.t_walk, st.t_record), MS(st.t_record, st.t_sort),
         MS(st.t_sort, st.t_artists), MS(st.t_artists, st.t_recent), MS(st.t_start, st.t_recent));
    log_printf("songs %u (seq err %u, dated %u), folders %u (dated %u, dropped %u), albums %u, "
         "album artists %u, artists %u\r\n",
         st.songs, st.seq_err, st.songs_dated, st.folders, st.folders_dated, st.folders_dropped, st.albums,
         lib[2], st.artists);
    log_printf("keys: compact %u, stock %u, chunk loads %u\r\n", st.key_hits, st.key_miss, st.key_loads);
    log_printf("cmp: %u calls, %u hash-equal, %u full (%u reads), hash loads %u, write errors %u\r\n",
         st.cmp_calls, st.cmp_hash_eq, st.cmp_full, st.cmp_reads, st.hash_loads, st.write_err);
    log_printf("us: walk hooks %u ms, key loads %u ms, cmp reads %u ms, hash loads %u ms\r\n",
         st.us_hooks / 1000, st.us_keyload / 1000, st.us_cmpread / 1000, st.us_hashload / 1000);
    tags_report();
    write_log();
}
