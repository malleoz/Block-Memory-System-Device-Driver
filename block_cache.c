////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_cache.c
//  Description    : This is the implementation of the cache for the BLOCK
//                   driver.
//
//  Author         : Sean Owens
//  Last Modified  : 7/24/2019
//

// Includes
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <assert.h>

// Project includes
#include <block_cache.h>
#include <cmpsc311_log.h>

uint32_t block_cache_max_items = DEFAULT_BLOCK_FRAME_CACHE_SIZE; // Maximum number of items in cache
int init = 0;

struct cache_frame *cache; // Declare structure
uint32_t cache_indeces_used;

//
// Functions

////////////////////////////////////////////////////////////////////////////////
//
// Function     : set_block_cache_size
// Description  : Set the size of the cache (must be called before init)
//
// Inputs       : max_frames - the maximum number of items your cache can hold
// Outputs      : 0 if successful, -1 if failure

int set_block_cache_size(uint32_t max_frames)
{
    // If the cache has already been initialized, then return an error
    if (init) {
	    return (-1);
    }

    // Set the cache length equal to max_frames
    block_cache_max_items = max_frames;
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : init_block_cache
// Description  : Initialize the cache and note maximum frames
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int init_block_cache(void)
{
    // Initialize the cache
    cache = malloc(block_cache_max_items * sizeof(struct cache_frame));
    cache_indeces_used = 0;
    init = 1;

    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : close_block_cache
// Description  : Clear all of the contents of the cache, cleanup
//
// Inputs       : none
// Outputs      : o if successful, -1 if failure

int close_block_cache(void)
{
    for (int i = 0; i < block_cache_max_items; i++) {
	    cache[i].frame_number = 0;
	    free(cache[i].frame);
	    cache[i].frame = NULL;
	    cache[i].calls_since_use = 0;
    }

    free(cache);
    cache = NULL;

    init = 0;
    
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : put_block_cache
// Description  : Put an object into the frame cache
//
// Inputs       : block - the block number of the frame to cache
//                frm - the frame number of the frame to cache
//                buf - the buffer to insert into the cache
// Outputs      : 0 if successful, -1 if failure

int put_block_cache(BlockIndex block, BlockFrameIndex frm, void* buf)
{
    // It's my understanding that block is irrelevant

    // Search through entire cache to see if frame #frm exists
    // Meanwhile, also keep track of the index with the least recently used frame, just in case it's a miss
    uint16_t least_recently_used = 0;
    uint16_t least_recent_index = 0;

    for (int i = 0; i < cache_indeces_used; i++) {
		// Increment the number of calls since the current index's frame has been fetched
		cache[i].calls_since_use++;
		
	    if (cache[i].frame_number == frm) {
		    // We found this frame in the cache!
		    // Set calls_since_use = 0
		    cache[i].calls_since_use = 0;

		    // Copy memory from buf to frames
		    memcpy(cache[i].frame, buf, BLOCK_FRAME_SIZE);

		    // Well, we wrote to the cache, so exit ok!
		    return(0);
	    }

	    // If the frame was not found, keep track of how many calls since this frame was referenced
	    else if (cache[i].calls_since_use > least_recently_used) {
			    least_recently_used = cache[i].calls_since_use;
			    least_recent_index = i;
	    }
    }

    // We searched all items in the cache and did not find the frame. Therefore, use replacement policy!
    // First check to see if there is a blank index in the cache
    if (cache_indeces_used < block_cache_max_items) {
	    // There's room at the cache_indeces_usedth index!
	    cache[cache_indeces_used].frame_number = frm;
	    cache[cache_indeces_used].calls_since_use = 0;
	    cache[cache_indeces_used].frame = malloc(BLOCK_FRAME_SIZE);
	    memcpy(cache[cache_indeces_used].frame, buf, BLOCK_FRAME_SIZE);
	    cache_indeces_used++;

	    // Exit ok!
	    return (0);
    }

    // Frame does not exist in cache, and there is no free room. Use LRU policy.
    cache[least_recent_index].frame_number = frm;
    cache[least_recent_index].calls_since_use = 0;
    memcpy(cache[least_recent_index].frame, buf, BLOCK_FRAME_SIZE);

    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : get_block_cache
// Description  : Get an frame from the cache (and return it)
//
// Inputs       : block - the block number of the block to find
//                frm - the  number of the frame to find
// Outputs      : pointer to cached frame or NULL if not found

void* get_block_cache(BlockIndex block, BlockFrameIndex frm)
{
    // Search through the cache
    for (int i = 0; i < cache_indeces_used; i++) {
	    if (cache[i].frame_number == frm) {
		    // We found the frame!
		    // Return the pointer
		    return (cache[i].frame);
	    }
    }

    return (NULL);
}


//
// Unit test

////////////////////////////////////////////////////////////////////////////////
//
// Function     : blockCacheUnitTest
// Description  : Run a UNIT test checking the cache implementation
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int blockCacheUnitTest(void)
{
    // Create a pointer array for the frame_data of each frame
    struct cache_frame_test *frame_test = calloc(CACHE_TEST_NUM_FRAMES, sizeof(struct cache_frame_test));

    // Create a buffer to store our randomized framedata in
    char *buf;

    // Initialize the cache
    init_block_cache();

    // Set the rand seed
    srand(time(NULL));

    // Loop through CACHE_TEST_NUM_LOOPS executions
    for (int i = 0; i < CACHE_TEST_NUM_LOOPS; i++) {
	    // Randomly determine a frame number from 0 to CACHE_TEST_NUM_FRAMES
	    int frame_num = rand() % CACHE_TEST_NUM_FRAMES;
	    printf("Frame: %d\n", frame_num);

	    // Allocate memory for the framedata
	    buf = malloc(BLOCK_FRAME_SIZE);
	    printf("Address of buf: %p\n", buf);

	    // Generate random framedata
	    for (int j = 0; j < BLOCK_FRAME_SIZE; j++) {
		    buf[j] = 33 + (rand() % 94); // Generate a random ASCII char between 33-126 inclusive
	    }

	    // Save to the pointer array
	    if (frame_test[frame_num].active == 0) {
		    frame_test[frame_num].active = 1;
		    frame_test[frame_num].data = malloc(BLOCK_FRAME_SIZE);
	    }

	    printf("Address of struct data: %p\n", frame_test[frame_num].data);

	    memcpy(frame_test[frame_num].data, buf, BLOCK_FRAME_SIZE);
	    printf("Buf: %s\nStruct: %s\n", buf, frame_test[frame_num].data);
	    
	    // Put in the cache
	    put_block_cache(0, frame_num, buf);

	    free(buf);

	    // Retrieve from the cache
	    buf = get_block_cache(0, frame_num);
	    printf("Address of returned data: %p\n", buf);
	    printf("Returned data: %s\n", buf);

	    // Assert that the retrieved data is the same as the data stored in the array
	    for (int j = 0; j < BLOCK_FRAME_SIZE; j++) {
		    if (buf[j] != frame_test[frame_num].data[j]) {
			    printf("Mismatch at char %d", j);
			    return (-1);
		    }
	    }

	    printf("Frame verified.\n\n");
    }

    // Return successfully
    printf("Successfully tested %d gets and puts!\n", CACHE_TEST_NUM_LOOPS);
    logMessage(LOG_OUTPUT_LEVEL, "Cache unit test completed successfully.");
    return (0);
}
