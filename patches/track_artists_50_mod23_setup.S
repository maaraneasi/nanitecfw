@ Track Artists: mod23 media browser (window 2), opened by the Media Library
@ "Artists" entry with MusicTypeSelID 3 and title 0x85 (library_menu_40).
@
@ artist_setup: level setup (called through here from window init, see
@ recent_albums_51_mod23_call.S). Runs recent_setup (stock setup + Recent
@ Albums), then for title 0x85 points the artist level at the record's track
@ artist (+0x100) and the levels at the tables the CFW blob builds at refresh.
@
@ artist_count: list refill (FUN_03079ef6) gets the item count from
@ FUN_03079418, which reads the top-level count from the library counters.
@ For title 0x85 at level 0 the count is u16[3] of the first top-level group
@ entry instead (written by the blob; stock never reads it). More than the song
@ count means the tables were never built: show an empty list.
@
@ Code cave: after shuffle_key in the unreferenced 0x03079d78..0x03079ef5.
@ patch 23 03079de0
#include "fw.inc"
    .equ    recent_setup, 0x03079d78
    .equ    item_count,   0x03079418
    .equ    group_field,  0x0307a0d8    @ (table sector, index, which): which 4 = u16[3]
    .equ    TITLE,        0x85
artist_setup:
    push    {r3, lr}
    fbl     recent_setup
    ldrh    r0, [r4, #2]                @ window param: title
    cmp     r0, #TITLE
    bne     1f
    ldr     r1, =0x0300e808             @ level descriptor: +0 base, +4 fields, +0xc tables
    movs    r0, #0x100                  @ REC_TRACK_ARTIST
    strh    r0, [r1, #4]
    ldr     r0, [r1]
    add     r0, r0, #0x52000
    add     r0, r0, #0x800              @ SEC_ARTIST_SONGS
    str     r0, [r1, #0x0c]
    adds    r0, #0x20                   @ SEC_ARTIST_GROUPS
    str     r0, [r1, #0x10]
    adds    r0, #0x80                   @ SEC_ARTIST_ALBUMS
    str     r0, [r1, #0x14]
1:  pop     {r3, pc}

artist_count:
    push    {r4, lr}
    fbl     item_count
    ldr     r4, =0x0300e800             @ browser title string id
    ldrh    r1, [r4]
    cmp     r1, #TITLE
    bne     1f
    ldrh.w  r1, [r4, #0x246]            @ cursor 0x0300ea3c: [5] level
    cbnz    r1, 1f
    ldr     r0, [r4, #0x18]             @ level descriptor table[1]: top-level groups
    movs    r1, #0
    movs    r2, #4
    fbl     group_field
    ldr     r1, =0x0300d164             @ library counters: +0x2e songs
    ldrh    r1, [r1, #0x2e]
    cmp     r0, r1
    it      hi
    movhi   r0, #0
1:  pop     {r4, pc}
    .ltorg
