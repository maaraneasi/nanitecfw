@ Shuffle: mod23 key handler "bl 0x03063242" -> shuffle_key.
@ patch 23 0307a796
#include "fw.inc"
    .equ    shuffle_key, 0x03079d98
    fbl     shuffle_key
