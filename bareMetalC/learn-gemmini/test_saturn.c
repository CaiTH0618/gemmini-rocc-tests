#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_params.h"
#include "include/gemmini.h"
#include "include/gemmini_testutils.h"
#include "util.h"


#ifndef NUM_CORES
#warning `NUM_CORES` is not set explicitly. Default to 1.
#define NUM_CORES 1
#endif 


int hart_main(int cid, int nc) {
    (void)cid;
    (void)nc;
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("Starting RVV test on core %d/%d\n", cid, nc);
        }
        barrier(nc);
    }

    // Enable vector state in mstatus (VS = Initial) for RVV instructions.
    unsigned long mstatus = 0;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus));
    mstatus |= (3UL << 8);
    __asm__ volatile ("csrw mstatus, %0" :: "r"(mstatus));
    __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus));
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("Enabled vector state in mstatus: 0x%lx\n", mstatus);
        }
        barrier(nc);
    }

    unsigned long misa = 0;
    __asm__ volatile ("csrr %0, misa" : "=r"(misa));
    const unsigned long vmask = 1UL << ('V' - 'A');
    if ((misa & vmask) == 0 || ((mstatus >> 8) & 0x3) == 0) {
        printf("RVV not enabled (misa=0x%lx, vs=%lu).\n", misa, (mstatus >> 8) & 0x3);
        return 0;
    }

    if (cid < 0 || cid >= NUM_CORES) {
        printf("Invalid core id %d\n", cid);
        return -1;
    }

    // Per-core RVV smoke test: set VL, load, add, store.
    static alignas(16) volatile uint32_t src[NUM_CORES][4] = {
        {1, 2, 3, 4},
        {5, 6, 7, 8},
        {9, 10, 11, 12},
        {13, 14, 15, 16},
    };
    static alignas(16) volatile uint32_t dst[NUM_CORES][4] = {0};

    volatile uint32_t *srcp = src[cid];
    volatile uint32_t *dstp = dst[cid];
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("Core %d source: %u %u %u %u\n", cid, srcp[0], srcp[1], srcp[2], srcp[3]);
        }
        barrier(nc);
    }

    __asm__ volatile (
        "li t0, 4\n"
        "vsetvli t1, t0, e32, m1\n"
        "vle32.v v0, (%0)\n"
        "vadd.vv v1, v0, v0\n"
        "vse32.v v1, (%1)\n"
        :
        : "r"(srcp), "r"(dstp)
        : "t0", "t1", "v0", "v1", "memory"
    );
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("Core %d dest: %u %u %u %u\n", cid, dstp[0], dstp[1], dstp[2], dstp[3]);
        }
        barrier(nc);
    }

    // Prevent optimizing away.
    asm volatile ("" :: "r"(dstp) : "memory");
    for (int i = 0; i < nc; i++) {
        if (i == cid) {
            printf("RVV test done on core %d: %u %u %u %u\n", cid, dstp[0], dstp[1], dstp[2], dstp[3]);
        }
        barrier(nc);
    }
}

void thread_entry(int cid, int nc) {
    // For multi-threaded program, start from this function instead of `main`.

    // Call custom function.
    hart_main(cid, nc);
    // Decide on returncode.
    int ret = 0;
    
    // Let one thread call `exit`.
    if (cid != 0) { while (1) {} }
    exit(ret);
}

int main() {
    return 1;
}