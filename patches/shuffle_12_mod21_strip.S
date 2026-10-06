@ Shuffle: main-menu paint, selection highlight after drawing the icon strip -> strip_extra (draws item base+3).
@ patch 21 0307a3bc
#include "fw.inc"
    .equ    strip_extra, 0x030795b4
    fbl     strip_extra
