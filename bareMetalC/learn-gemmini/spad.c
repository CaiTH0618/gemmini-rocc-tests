#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif

// #define SPAD_ADDR_BASE 0x08000000U  // mbus scratchpad in `GemminiLearningConfigWithScratchpad`
#define SPAD_ADDR_BASE 0xC0000000U  // sbus scratchpad in `GemminiLearningConfigWithScratchpad`
// #define SPAD_ADDR_SIZE 0x00010000U  // default Scrachpad size in `AbstractConfig`
#define SPAD_ADDR_SIZE 0x00100000U  // bigger Scratchpad size in `GemminiLearningConfigWithScratchpad`
#define SPAD_ADDR_CEIL (SPAD_ADDR_BASE + SPAD_ADDR_SIZE)

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))


static inline uint64_t read_cycles() {
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;
}

static inline void fence_rw_rw(void) {
    /* RISC-V full fence for ordering (if running on RISC-V); adjust per arch */
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline void write_spad_uint64(uint64_t addr, uint64_t value) {
    volatile uint64_t* p = (volatile uint64_t*) addr;
    *p = value;
    fence_rw_rw();
}

static inline uint64_t read_spad_uint64(uint64_t addr) {
    volatile uint64_t* p = (volatile uint64_t*) addr;
    fence_rw_rw();
    return *p;
}


int main() {
    printf("\ndefault_mbus_spad starts\n");

    const uint64_t test_spad_addr_size = 0x00100000U;  // 1MB 
    // const uint64_t test_spad_addr_size = 0x00010000U;  // 64KB
    // const uint64_t test_spad_addr_size = 0x00001000U;  // 4KB
    // const uint64_t test_spad_addr_size = 0x00000100U;  // 256B
    // const uint64_t test_spad_addr_size = 0x00000040U;  // 64B
    if (test_spad_addr_size > SPAD_ADDR_SIZE) {
        printf("test_spad_addr_size is too large\n");
        return 1;
    }

    const uint64_t test_spad_addr_ceil = SPAD_ADDR_BASE + test_spad_addr_size;
    const uint64_t stride = 64;  // cache block
    const uint64_t num = test_spad_addr_size / stride;

    printf("Write all values from spad in order.\n");
    {
        uint64_t c_total = 0;
        uint64_t c_min = 1000;
        uint64_t c_max = 0;
        for (uint64_t addr = SPAD_ADDR_BASE; addr < test_spad_addr_ceil; addr += stride) {
            uint64_t value = addr - SPAD_ADDR_BASE;
            uint64_t c_start = read_cycles();
            write_spad_uint64(addr, value);
            uint64_t c_end = read_cycles();
            uint64_t c = c_end - c_start;
            c_total += c;
            c_min = MIN(c, c_min);
            c_max = MAX(c, c_max);
            if (addr % 0x10000U == 0) {
                printf("%lu, ", c);
            }
        }
        printf("\nnum: %lu, avg: %lu, min: %lu, max: %lu\n\n", 
            num, c_total / num, c_min, c_max);
    }

    for (int i = 0; i < 5; i++) {
        printf("Read all values from spad in order.\n");
        uint64_t c_total = 0;
        uint64_t c_min = 1000;
        uint64_t c_max = 0;
        for (uint64_t addr = SPAD_ADDR_BASE; addr < test_spad_addr_ceil; addr += stride) {
            uint64_t c_start = read_cycles();
            uint64_t value = read_spad_uint64(addr);
            uint64_t c_end = read_cycles();
            if (value != addr - SPAD_ADDR_BASE) {
                printf("Error: %lu read, %lu expected\n", value, addr - SPAD_ADDR_BASE);
                return 2;
            }
            uint64_t c = c_end - c_start;
            c_total += c;
            c_min = MIN(c, c_min);
            c_max = MAX(c, c_max);
            if (addr % 0x10000U == 0) {
                printf("%lu, ", c);
            }
        }
        printf("\nnum: %lu, avg: %lu, min: %lu, max: %lu\n\n", 
            num, c_total / num, c_min, c_max);
    }
    
    printf("default_mbus_spad ends\n\n");
    return 0;
}