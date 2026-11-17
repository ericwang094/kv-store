#include "time_util.h"

long time_sub_ms(const struct timeval *tv1, const struct timeval *tv2) {
    return (tv1->tv_sec - tv2->tv_sec) * 1000 + (tv1->tv_usec - tv2->tv_usec) / 1000;
}

