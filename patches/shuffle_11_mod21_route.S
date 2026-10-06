@ Shuffle: mod21 dispatcher, Media Library path "ldr r1,=MusicTypeSelID; movs r0,#0"
@ -> shuffle_route (loads r1/r0 itself, returns to 0x03079b90 for other items).
@ patch 21 03079b8c
#include "fw.inc"
    .equ    shuffle_route, 0x03079544
    fb      shuffle_route
