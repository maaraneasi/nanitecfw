@ Track Artists: mod23 list refill FUN_03079ef6 "bl FUN_03079418" -> artist_count
@ (track_artists_50_mod23_setup.S).
@ patch 23 03079ef8
#include "fw.inc"
    .equ    artist_count, 0x03079e0a
    fbl     artist_count
