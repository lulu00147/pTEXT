#define _GNU_SOURCE
#include "affinity.h"

#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

static cpu_set_t s_big;
static cpu_set_t s_little;
static cpu_set_t s_allowed;
static int s_ready;
static int s_mode;

static long cpu_max_freq(int cpu)
{
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long khz = 0;
    if (fscanf(f, "%ld", &khz) != 1) khz = 0;
    fclose(f);
    return khz;
}

static void detect(void)
{
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    int have = 0;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (CPU_ISSET(cpu, &allowed)) { have = 1; break; }
        }
    }
    if (!have) {
        CPU_ZERO(&allowed);
        CPU_SET(0, &allowed);
    }
    s_allowed = allowed;

    long max_khz = 0, min_khz = LONG_MAX;
    int any_freq = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (!CPU_ISSET(cpu, &allowed)) continue;
        long khz = cpu_max_freq(cpu);
        if (khz <= 0) continue;
        any_freq = 1;
        if (khz > max_khz) max_khz = khz;
        if (khz < min_khz) min_khz = khz;
    }

    CPU_ZERO(&s_big);
    CPU_ZERO(&s_little);
    if (!any_freq || max_khz == min_khz) {
        s_big = allowed;
        s_little = allowed;
    } else {
        long threshold = min_khz + (max_khz - min_khz) / 2;
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (!CPU_ISSET(cpu, &allowed)) continue;
            long khz = cpu_max_freq(cpu);
            if (khz >= threshold) CPU_SET(cpu, &s_big);
            else CPU_SET(cpu, &s_little);
        }
        int big_count = 0, little_count = 0;
        for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
            if (CPU_ISSET(cpu, &s_big)) big_count++;
            if (CPU_ISSET(cpu, &s_little)) little_count++;
        }
        if (!big_count) s_big = allowed;
        if (!little_count) s_little = allowed;
    }
    s_ready = 1;
}

static void bind_mask(const cpu_set_t *mask)
{
    if (mask) (void)sched_setaffinity(0, sizeof(*mask), mask);
}

static const cpu_set_t *current_mask(void)
{
    if (!s_ready) detect();
    return s_mode == 0 ? &s_big : s_mode == 1 ? &s_little : &s_allowed;
}

void affinity_set_mode(int mode)
{
    s_mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
    bind_mask(current_mask());
}

void affinity_bind_current(void)
{
    bind_mask(current_mask());
}

#ifdef PTEXT_HOST_TEST
int affinity_test_mode(void) { return s_mode; }
int affinity_test_ready(void) { return s_ready; }
#endif
