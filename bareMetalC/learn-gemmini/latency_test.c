#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif

#define ADDR_SIZE 0x00100000U
// #define ADDR_SIZE 0x00010000U

#define SBUS_SPAD_ADDR_BASE 0xC0000000U
#define SBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define SBUS_SPAD_ADDR_CEIL (SBUS_SPAD_ADDR_BASE + SBUS_SPAD_ADDR_SIZE)

#define MBUS_SPAD_ADDR_BASE 0x08000000U
#define MBUS_SPAD_ADDR_SIZE ADDR_SIZE
#define MBUS_SPAD_ADDR_CEIL (MBUS_SPAD_ADDR_BASE + MBUS_SPAD_ADDR_SIZE)

#define MEM_BUF_SIZE (ADDR_SIZE / sizeof(uint64_t))
static uint64_t mem_buf[MEM_BUF_SIZE];
static uint64_t mem_buf_head_addr = (uint64_t) mem_buf;

#define MEM_ADDR_BASE mem_buf_head_addr
#define MEM_ADDR_SIZE (MEM_BUF_SIZE * sizeof(uint64_t))
#define MEM_ADDR_CEIL (MEM_ADDR_BASE + MEM_ADDR_SIZE)

// #define PTR_CHASING_UNIT sizeof(uint64_t)
#define PTR_CHASING_UNIT 0x40
// #define PTR_CHASING_UNIT 0x100
#define PTR_CHASING_N_UINT64_PER_UNIT (PTR_CHASING_UNIT / sizeof(uint64_t))
#define PTR_CHASING_BUF_SIZE (ADDR_SIZE / PTR_CHASING_UNIT)


static inline uint64_t read_cycles() {
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;
}

static inline void fence_rw_rw(void) {
    /* RISC-V full fence for ordering (if running on RISC-V); adjust per arch */
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static inline uint32_t rand_with_range(uint32_t low, uint32_t high) {
    static uint64_t rand_prng_state = 0x9E3779B97F4A7C15ULL;
    // Update state using LCG (Linear Congruential Generator)
    // Using constants from Knuth's MMIX LCG for good statistical properties
    rand_prng_state = rand_prng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    // Return value in range [low, high)
    if (high <= low) {
        return low;
    }
    // Use upper 32 bits for better distribution
    uint32_t random_value = (uint32_t)(rand_prng_state >> 32);
    return low + (random_value % (high - low));
}


uint64_t latency_test(uint64_t addr_base, uint64_t addr_ceil) {
    printf("latency_test: starts\n");

    // Calculate the number of 64-bit words in the memory region
    if (addr_ceil - addr_base != ADDR_SIZE) {
        printf("latency_test: error: address region is incorrect\n");
        return 0;
    }

    // Cast the base address to a pointer to the memory region we'll test
    volatile uint64_t *region = (volatile uint64_t *)(uintptr_t)addr_base;

    printf("latency_test: initialize index buffer\n");
    // Allocate a buffer to hold the random permutation of indices
    static uint32_t index_buffer[PTR_CHASING_BUF_SIZE];
    for (uint32_t i = 0; i < PTR_CHASING_BUF_SIZE; ++i) {
        // rand an offset as number of uint64_t elements
        uint32_t offset = rand_with_range(0, PTR_CHASING_N_UINT64_PER_UNIT);
        // Store index into uint64_t array (not byte offset)
        index_buffer[i] = i * PTR_CHASING_N_UINT64_PER_UNIT + offset;
    }

    // Run multiple rounds and average the results for stability
    const uint64_t chase_rounds = 5;
    // const uint64_t chase_rounds = 10;
    // const uint64_t chases_per_round = 100;
    // const uint64_t chases_per_round = 1000;
    const uint64_t chases_per_round = 10000;
    if (PTR_CHASING_BUF_SIZE < chases_per_round) {
        printf("latency_test: error: region is too small\n");
        return 0;
    }

    uint64_t accumulated_latency = 0;
    for (uint64_t round = 0; round < chase_rounds; ++round) {
        printf("latency_test: round %lu\n", round);

        // Step 2: Partial Fisher-Yates shuffle - only shuffle the first chases_per_round elements
        // This is much faster than shuffling the entire array
        printf("latency_test: index shuffling\n");
        for (uint32_t i = 0; i < chases_per_round; ++i) {
            uint32_t j = rand_with_range(i + 1, PTR_CHASING_BUF_SIZE);
            // Swap elements at positions i and j
            uint32_t tmp = index_buffer[i];
            index_buffer[i] = index_buffer[j];
            index_buffer[j] = tmp;
        }

        // Step 3: Build the pointer chain in memory
        // Each location points to the next location in the permutation sequence
        // This creates a dependency chain: accessing location N requires reading location N-1
        printf("latency_test: memory filling\n");
        for (uint32_t i = 0; i < chases_per_round; ++i) {
            uint32_t curr = index_buffer[i];
            uint32_t next = index_buffer[(i + 1) % chases_per_round];
            // Store the ADDRESS of the next location, not the index
            region[curr] = (uint64_t)(&(region[next]));
        }

        printf("latency_test: point chasing\n");
        // Step 4: Perform the pointer chase
        // Start with the address of the first element in the chain
        volatile uint64_t *current_ptr = &(region[index_buffer[0]]);
        fence_rw_rw();
        uint64_t start = read_cycles();
        // Chase through all locations in the chain
        // Each iteration depends on the previous load completing
        for (uint64_t step = 0; step < chases_per_round; ++step) {
            // Dereference to get the next address in the chain
            current_ptr = (volatile uint64_t *)(*current_ptr);
        }
        uint64_t end = read_cycles();
        fence_rw_rw();

        // Calculate average latency per access (scaled by 1000 for precision)
        uint64_t elapsed = end - start;
        accumulated_latency += elapsed;
        printf("latency_test: latency: %lu * 0.001 cycles\n", 
            (elapsed * 1000) / chases_per_round);
    }

    printf("latency_test: ends\n");
    // Return the average latency across all rounds (in units of 0.001 cycles)
    return (accumulated_latency * 1000) / (chase_rounds * chases_per_round);
}


int main() {
    printf("latency_test main: starts\n");

    uint64_t mem_latency = latency_test(MEM_ADDR_BASE, MEM_ADDR_CEIL);
    printf("memory latency : %lu * 0.001 cycles\n\n", mem_latency);

    uint64_t mbus_spad_latency = latency_test(MBUS_SPAD_ADDR_BASE, MBUS_SPAD_ADDR_CEIL);
    printf("mbus-spad latency : %lu * 0.001 cycles\n\n", mbus_spad_latency);

    uint64_t sbus_spad_latency = latency_test(SBUS_SPAD_ADDR_BASE, SBUS_SPAD_ADDR_CEIL);
    printf("sbus-spad latency : %lu * 0.001 cycles\n\n", sbus_spad_latency);

    printf("latency_test main: ends\n");
    return 0;
}
