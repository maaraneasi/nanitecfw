#ifndef PROF_H
#define PROF_H
#include "fw.h"

u32 now_us(void);   /* microseconds, wraps after ~71 min; use differences */
void prof_report(void (*out)(const char *fmt, ...));

#endif
