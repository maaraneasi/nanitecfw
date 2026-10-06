@ Example code patch syntax only - the address below is live code; do NOT build as-is.
@ Copy to patches/ and set the real module/address to include it in the build.
@ patch 0 03050100
#include "fw.inc"             @ firmware addresses + fbl/fb/fbeq/fbne branch macros
    nop
    nop
