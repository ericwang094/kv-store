#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <sys/time.h>

long time_sub_ms(const struct timeval *tv1, const struct timeval *tv2);

#endif
