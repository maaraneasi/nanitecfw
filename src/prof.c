/* Call-site profiler for src/hooks.txt "wrap" sites (see prof_thunk in start.S). */
#include "fw.h"
#include "prof.h"

struct site { u32 ret, target; const char *tag; };

/* src/hooks.txt has no wrap sites at the moment; the table ends with a 0 entry */
static const struct site sites[] = {
#define WRAP(ret, target, tag) { ret, target, tag },
#include "wraps.h"
#undef WRAP
    { 0, 0, 0 }
};
#define NSITES (sizeof sites / sizeof sites[0])     /* including the end marker */

static u32 calls[NSITES], total_us[NSITES], max_us[NSITES];
static u32 cur, t0;
u32 prof_save[8];   /* prof_thunk scratch: r0-r3, lr, target, result r0/r1 */

#define TICKS    (*(volatile u32 *)0x0300d144)    /* 10 ms */
#define SYST_RVR (*(volatile u32 *)0xe000e014)
#define SYST_CVR (*(volatile u32 *)0xe000e018)

u32 now_us(void)
{
    u32 t, c;
    do {
        t = TICKS;
        c = SYST_CVR;
    } while (t != TICKS);
    u32 rvr = SYST_RVR;
    return t * 10000 + (rvr - c) / ((rvr + 1) / 10000);   /* SysTick counts 10 ms */
}

/* returns the original target (thumb) for the wrapped site that called us */
u32 prof_begin(u32 lr)
{
    lr &= ~1u;
    for (u32 i = 0; sites[i].ret; i++)
        if (sites[i].ret == lr) {
            cur = i;
            t0 = now_us();
            return sites[i].target | 1;
        }
    for (;;)
        ;   /* unreachable: every wrap site is in the table */
}

void prof_end(void)
{
    u32 dt = now_us() - t0;
    calls[cur]++;
    total_us[cur] += dt;
    if (dt > max_us[cur])
        max_us[cur] = dt;
}

/* one line per tag (sites with the same tag summed) */
void prof_report(void (*out)(const char *fmt, ...))
{
    for (u32 i = 0; sites[i].ret; i++) {
        u32 seen = 0;
        for (u32 j = 0; j < i; j++)
            if (!__builtin_strcmp(sites[j].tag, sites[i].tag))
                seen = 1;
        if (seen)
            continue;
        u32 n = 0, us = 0, mx = 0;
        for (u32 j = i; sites[j].ret; j++)
            if (!__builtin_strcmp(sites[j].tag, sites[i].tag)) {
                n += calls[j];
                us += total_us[j];
                if (max_us[j] > mx)
                    mx = max_us[j];
            }
        out("  %-9s %6u calls %7u ms  max %5u ms  (%x)\r\n", sites[i].tag, n, us / 1000, mx / 1000, sites[i].target);
    }
}
