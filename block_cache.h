#ifndef BLOCK_FRAME_CACHE_INCLUDED
#define BLOCK_FRAME_CACHE_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_cache.h
//  Description    : This is the header file for the implementation of the
//                   frame cache for the BLOCK memory system driver.
//
//  Author         : Patrick McDaniel
//  Last Modified  : Mon Jul 8 00:00:00 EDT 2019
//

// Includes
#include <block_controller.h>

// Defines
#define DEFAULT_BLOCK_FRAME_CACHE_SIZE 1024 // Default size for cache
#define CACHE_TEST_NUM_FRAMES 20 // Number of frames we want to use for the unit test
#define CACHE_TEST_NUM_LOOPS 10000 // Number of iterations of tests

///
// Cache Interfaces

int set_block_cache_size(uint32_t max_frames);
// Set the size of the cache (must be called before init)

int init_block_cache(void);
// Initialize the cache

int close_block_cache(void);
// Clear all of the contents of the cache, cleanup

int put_block_cache(BlockIndex blk, BlockFrameIndex frm, void* frame);
// Put an object into the object cache, evicting other items as necessary

void* get_block_cache(BlockIndex blk, BlockFrameIndex frm);
// Get an object from the cache (and return it)

struct cache_frame {
    uint16_t frame_number; // The frame number at this entry in the cache
    uint16_t calls_since_use; // Due to LRU policy, keep track of how many cache calls have been made since this frame was referenced
    void *frame; // Pointer to framedata
} cache_frame;

//
// Unit test

int blockCacheUnitTest(void);
// Run a UNIT test checking the cache implementation

struct cache_frame_test {
	int active;
	char *data;
} cache_frame_test;

#endif
