#include "encoding.h"
#include "util.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifndef NUM_CORES
#define NUM_CORES 4
#endif


void func(int cid) {
    uint32_t hart = read_csr(mhartid);
    for (int j = 0; j < NUM_CORES; j++) {
        if (hart == j) {
            printf("Hello world! cid=%d, hart=%u\n", cid, hart);
        }
        barrier(NUM_CORES);
    }
}


void thread_entry(int cid, int nc) {
    for (int j = 0; j < NUM_CORES; j++) {
        if (cid == j) {
            printf("cid=%d, nc=%d\n", cid, nc);
        }
        barrier(NUM_CORES);
    }

    func(cid);

    if (cid == 0) {
        exit(0);
    } else {
        while (1) {}
    }
}


int main() {
    printf("main: multi-hart should not run this function.\n");
    return 0;
}
