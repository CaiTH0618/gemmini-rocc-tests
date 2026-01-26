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
    printf("Starting RVV test on core %d/%d\n", cid, nc);

    // Enable vector state in mstatus (VS = Initial) for RVV instructions.
    unsigned long mstatus = 0;
    __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus));
    mstatus |= (3UL << 8);
    __asm__ volatile ("csrw mstatus, %0" :: "r"(mstatus));
    __asm__ volatile ("csrr %0, mstatus" : "=r"(mstatus));
    printf("Enabled vector state in mstatus: 0x%lx\n", mstatus);

    unsigned long misa = 0;
    __asm__ volatile ("csrr %0, misa" : "=r"(misa));
    const unsigned long vmask = 1UL << ('V' - 'A');
    if ((misa & vmask) == 0 || ((mstatus >> 8) & 0x3) == 0) {
        printf("RVV not enabled (misa=0x%lx, vs=%lu).\n", misa, (mstatus >> 8) & 0x3);
        return 0;
    }

    // Basic RVV smoke test: set VL, load, add, store.
    alignas(16) volatile uint32_t src[4] = {1, 2, 3, 4};
    alignas(16) volatile uint32_t dst[4] = {0, 0, 0, 0};
    printf("Source data: %u %u %u %u\n", src[0], src[1], src[2], src[3]);

    __asm__ volatile (
        "li t0, 4\n"
        "vsetvli t1, t0, e32, m1\n"
        "vle32.v v0, (%0)\n"
        "vadd.vv v1, v0, v0\n"
        "vse32.v v1, (%1)\n"
        :
        : "r"(src), "r"(dst)
        : "t0", "t1", "v0", "v1", "memory"
    );
    printf("Destination data after RVV add: %u %u %u %u\n", dst[0], dst[1], dst[2], dst[3]);

    // Prevent optimizing away.
    asm volatile ("" :: "r"(dst) : "memory");
    printf("RVV test done: %u %u %u %u\n", dst[0], dst[1], dst[2], dst[3]);
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