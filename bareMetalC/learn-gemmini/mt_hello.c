#include "encoding.h"
#include "util.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>


#ifndef NUM_CORES
#warning `NUM_CORES` is not set explicitly. Default to 1.
#define NUM_CORES 1
#endif 


static inline uint32_t get_hart_id() {
    return read_csr(mhartid);
}


void func(int cid, int nc) {
    uint32_t hart = get_hart_id();
    for (int j = 0; j < nc; j++) {
        if (cid == j) {
            printf("Hello world! cid=%d, hart=%u, nc=%d\n", cid, hart, nc);
        }
        barrier(nc);
    }
}

void thread_entry(int cid, int nc) {
    // For multi-threaded program, start from this function instead of `main`.

    // Call custom function.
    func(cid, nc);
    // Decide on returncode.
    int ret = 0;
    
    // Let one thread call `exit`.
    if (cid != 0) { while (1) {} }
    exit(ret);
}

int main() {
    // `main` is called after `thread_entry` return in `_init` (syscall.c).
    // But only single-threaded program should run and return from `main`.

    // Guard on other threads that fall back in `main`.
    if (get_hart_id() != 0) { while (1) {} }
    return 0;
}
