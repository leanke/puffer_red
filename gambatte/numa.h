/*
 * numa.h — Lightweight NUMA topology detection and memory/thread binding.
 *
 * No libnuma dependency: topology is read from sysfs, memory policy is set
 * via the set_mempolicy(2) syscall, and thread affinity via sched_setaffinity.
 *
 * On non-NUMA (single-socket) systems numa_detect() returns num_nodes=1 and
 * all NUMA paths are skipped, so there is no overhead.
 */
#ifndef GAMBATTE_NUMA_H
#define GAMBATTE_NUMA_H

/* Required for cpu_set_t, CPU_SET, sched_setaffinity on Linux. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#  include <sys/syscall.h>
/* mempolicy constants — avoid pulling in numaif.h */
#  define _MPOL_DEFAULT   0
#  define _MPOL_PREFERRED 1
#endif

#define MAX_NUMA_NODES 8

typedef struct {
    int       num_nodes;
    cpu_set_t cpusets[MAX_NUMA_NODES];
    int       num_cpus[MAX_NUMA_NODES];
} NumaTopology;

/* Parse a cpulist like "0-15,32-47" into a cpu_set_t. */
static void _parse_cpulist(const char *s, cpu_set_t *set, int *count) {
    CPU_ZERO(set);
    *count = 0;
    while (*s && *s != '\n') {
        int lo = 0, hi;
        while (*s >= '0' && *s <= '9') lo = lo * 10 + (*s++ - '0');
        hi = lo;
        if (*s == '-') {
            s++; hi = 0;
            while (*s >= '0' && *s <= '9') hi = hi * 10 + (*s++ - '0');
        }
        for (int c = lo; c <= hi; c++) { CPU_SET(c, set); (*count)++; }
        if (*s == ',') s++;
    }
}

/*
 * Populate *t from /sys/devices/system/node/node{N}/cpulist.
 * Falls back to a single node covering all online CPUs on non-NUMA systems.
 */
static void numa_detect(NumaTopology *t) {
    memset(t, 0, sizeof(*t));
    for (int n = 0; n < MAX_NUMA_NODES; n++) {
        char path[80], buf[256] = {0};
        snprintf(path, sizeof(path),
                 "/sys/devices/system/node/node%d/cpulist", n);
        FILE *f = fopen(path, "r");
        if (!f) break;
        if (fgets(buf, sizeof(buf), f))
            _parse_cpulist(buf, &t->cpusets[n], &t->num_cpus[n]);
        fclose(f);
        t->num_nodes++;
    }
    if (t->num_nodes == 0) {
        t->num_nodes = 1;
        long np = sysconf(_SC_NPROCESSORS_ONLN);
        for (int c = 0; c < (int)np; c++) CPU_SET(c, &t->cpusets[0]);
        t->num_cpus[0] = (int)np;
    }
}

/*
 * Bias subsequent calloc/malloc on the calling thread to prefer `node`.
 * Uses MPOL_PREFERRED so the system can fall back if the node is full.
 */
static void numa_set_mem_policy(int node) {
#if defined(__linux__) && defined(SYS_set_mempolicy)
    unsigned long mask = 1UL << node;
    syscall(SYS_set_mempolicy, _MPOL_PREFERRED, &mask, MAX_NUMA_NODES + 1);
#else
    (void)node;
#endif
}

/* Restore the default (local-first) allocation policy. */
static void numa_clear_mem_policy(void) {
#if defined(__linux__) && defined(SYS_set_mempolicy)
    syscall(SYS_set_mempolicy, _MPOL_DEFAULT, NULL, 0);
#endif
}

/* Pin the calling thread's CPU affinity to the CPUs of `node`. */
static void numa_bind_thread(const NumaTopology *t, int node) {
    if (node >= 0 && node < t->num_nodes)
        sched_setaffinity(0, sizeof(cpu_set_t), &t->cpusets[node]);
}

#endif /* GAMBATTE_NUMA_H */
