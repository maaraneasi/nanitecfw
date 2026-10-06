@ Recent Albums / Track Artists: mod23 FUN_03079f1e "bl FUN_0307a144" ->
@ artist_setup (track_artists_50_mod23_setup.S), which runs recent_setup.
@ patch 23 03079f22
#include "fw.inc"
    .equ    artist_setup, 0x03079de0
    fbl     artist_setup
