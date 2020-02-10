////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the BLOCK storage system.
//
//  Author         : Sean Owens
//

// Includes
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Project Includes
#include <block_controller.h>
#include <block_driver.h>
#include <cmpsc311_util.h>
#include <block_cache.h>

// Create an array that will keep track of all files
struct file *all_files;

// Keep track of the number of frames used
uint16_t num_frames_used;

// Keep track of the number of files in the list
uint16_t num_files;

// Pointer to store the frame checksum of the data retrieved from the block system
uint32_t *fr_checksum;

// Variable to store value at fr_checksum
uint32_t fr_checksum_value;

// Temporary buffer used for reading/writing
char *temp_buf;

// Temporary buffer used for reading from the cache
char *cache_buf;

//
// Implementation

////////////////////////////////////////////////////////////////////////////////
//
// Function	: generate_register
// Description	: Take in kr1, fm1, cs1, rt1 to generate the register to communicate with hardware device
//
// Inputs	: kr1, fm1, cs1, rt1
// Outputs	: int64_t type register

uint64_t generate_register(uint8_t kr1, uint16_t fm1, uint32_t cs1, uint8_t rt1)
{
	uint64_t reg = 0x0, tempkr1, tempfm1, tempcs1, temprt1;

	// Shift each variable to its designated spot in the 64-bit register
	tempkr1 = ((uint64_t)kr1) << 56;
	tempfm1 = ((uint64_t)fm1) << 40;
	tempcs1 = ((uint64_t)cs1) << 32;
	tempcs1 = tempcs1 >> 24;
	tempcs1 = tempcs1 & 0x000000ffffffffff;
	temprt1 = ((uint64_t)rt1);
	
	// Combine all variables into one 64-bit register
	reg = tempkr1|tempfm1|tempcs1|temprt1;

	return reg;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_poweron
// Description  : Startup up the BLOCK interface, initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t block_poweron(void)
{
    // Specify init opcode
    BlockOpCodes opcode = BLOCK_OP_INITMS;
   
    // Generate register
    BlockXferRegister reg = generate_register(opcode, 0, 0, 0);
    
    // Create variable to store return reg
    BlockXferRegister return_reg;

    // Pass to hardware device
    return_reg = block_io_bus(reg, 0);

    // block_io_bus returns rt1 = -1 if there was an error
    return_reg = return_reg << 56;
    return_reg = return_reg >> 56;
    int8_t rt1 = return_reg;
    
    if (rt1 == -1) {
	    return (-1);
    }

    // Assign global variables
    num_frames_used = 0;
    num_files = 0;
    temp_buf = malloc(BLOCK_FRAME_SIZE);
    fr_checksum = malloc(sizeof(uint32_t));

    // Create a FILE *file pointer to see if block_memsys.bck exists
    FILE *file = fopen("block_memsys.bck", "r");

    if (file != NULL) {
	    // block_memsys.bck exists! Read frame 0 and extract relevant metadata for my data structures

	    // Declare a variable to keep track of the checksum returned by the frame
        uint32_t frame_checksum;

	    // Specify read opcode
	    opcode = BLOCK_OP_RDFRME;

	    // Generate register
	    reg = generate_register(opcode, 0, 0, 0);

	    // Pass through io bus
	    return_reg = block_io_bus(reg, temp_buf);

	    uint64_t rt_temp = return_reg << 56;
	    rt_temp = rt_temp >> 56;
	    rt1 = rt_temp;

	    if (rt1 == -1) {
		    return (-1);
	    }

	    // Observe the checksum of the frame returned by block_io_bus
	    return_reg = return_reg << 24;
	    return_reg = return_reg >> 32;
	    frame_checksum = return_reg;

	    // Calculate the checksum of the received framedata
	    compute_frame_checksum(temp_buf, fr_checksum);
	    fr_checksum_value = (uint32_t) *fr_checksum;

	    while (frame_checksum != fr_checksum_value) {
		    // Checksums do not match! Call block_io_bus until they match
		    reg = generate_register(opcode, 0, 0, 0);
		    return_reg = block_io_bus(reg, temp_buf);

		    rt_temp = return_reg << 56;
		    rt_temp = rt_temp >> 56;
		    rt1 = rt_temp;

		    if (rt1 == -1) {
			    return (-1);
		    }

		    compute_frame_checksum(temp_buf, fr_checksum);
		    fr_checksum_value = (uint32_t) *fr_checksum;
	    }

	    // Metadata is now extracted into temp_buf!

	    // Keep a variable to track how many bytes have been written from the metadata so far
	    int bytes_read;

	    // The first uint16_t 2 bytes of the buffer signify how many files there are
	    memcpy(&num_files, temp_buf, 2);
	    bytes_read = 2;

	    // Allocate memory for all_files based on num_files
	    all_files = (struct file*) realloc(all_files, sizeof(struct file) * num_files);

	    // For num_files files, go through each file
	    for (int i = 0; i < num_files; i++) {
		    // Copy over the file's path
		    memcpy(&all_files[i].path, temp_buf+bytes_read, BLOCK_MAX_PATH_LENGTH);
		    bytes_read += BLOCK_MAX_PATH_LENGTH;

		    // Copy over the file's handle
		    memcpy(&all_files[i].handle, temp_buf+bytes_read, sizeof(int16_t));
		    bytes_read += 2;

		    // Copy over the file's length
		    memcpy(&all_files[i].length, temp_buf+bytes_read, sizeof(uint32_t));
		    bytes_read += 4;

		    // Set the file's status to open and its seek position to 0
		    all_files[i].status = OPEN;
		    all_files[i].seek_pos = 0;

		    // Specify the number of frames for the file
		    memcpy(&all_files[i].num_frames, temp_buf+bytes_read, sizeof(uint16_t));
		    bytes_read += 2;

		    // Allocate memory for all_files[i].frames based on num_frames
		    all_files[i].frames = calloc(all_files[i].num_frames, sizeof(uint16_t));

		    // Jot down each of the frames that the file uses based off of num_frames
		    for (int j = 0; j < all_files[i].num_frames; j++) {
			    memcpy(&all_files[i].frames[j], temp_buf+bytes_read, sizeof(uint16_t));
			    bytes_read += 2;
		    }

		    // All data for the current file has been restored!
	    }

	    // All data for all files has been restored!
    }

    // Initiate the cache
    // Cache size is set by the main function. Therefore, we don't need to worry about calling set_block_cache_size
    init_block_cache();
	cache_buf = malloc(BLOCK_FRAME_SIZE);

    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_poweroff
// Description  : Shut down the BLOCK interface, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t block_poweroff(void)
{
    // Declare opcode
    BlockOpCodes opcode;

    // Declare register
    BlockXferRegister reg;

    // Create variable to hold return reg
    BlockXferRegister return_reg;

    // On BLOCK_OP_POWOFF, the state of the filesystem is stored to block_memsys.bck in our directory.
    // When shutting down place metadata of our data structures in the first frame, which should have been skipped when reading/writing

    // Keep track of how many bytes we've written since the start of the buffer
    int bytes_written;

    // First we need to store the number of files, which is a uint16_t
    memcpy(temp_buf, &num_files, sizeof(uint16_t));
    bytes_written = 2;

    // Now go through each file and write bytes regarding that file's metadata
    for (int i = 0; i < num_files; i++) {
	    // First copy over the path
	    memcpy(temp_buf + bytes_written, &all_files[i].path, BLOCK_MAX_PATH_LENGTH);
	    bytes_written += BLOCK_MAX_PATH_LENGTH;

	    // Jot down the file handle
	    memcpy(temp_buf + bytes_written, &all_files[i].handle, sizeof(int16_t));
	    bytes_written += 2;

	    // Jot down the file length
	    memcpy(temp_buf + bytes_written, &all_files[i].length, sizeof(uint32_t));
	    bytes_written += 4;

	    // We do not need to jot down the status nor the seek position b/c when we restart the block system, these files will be closed
	    // Jot down the number of frames that the file uses
	    memcpy(temp_buf + bytes_written, &all_files[i].num_frames, sizeof(uint16_t));
	    bytes_written += 2;

	    // Now we need to jot down which frames correspond with this file
	    for (int j = 0; j < all_files[i].num_frames; j++) {
		   memcpy(temp_buf + bytes_written, &all_files[i].frames[j], sizeof(uint16_t));
		   bytes_written += 2;
	    }

	    // All file data for the current file has been stored!
    }

    // All file data is stored in the buffer!
    // Populate the rest of the buffer with bytes of 0, to get rid of garbage data and signify the end of my metadata
    for (int i = bytes_written; i < BLOCK_FRAME_SIZE; i++) {
	    temp_buf[i] = 0;
    }

    // Alright! Metadata should now correctly be stored in the first frame of the block system.

    // Write to the block system...

    compute_frame_checksum(temp_buf, fr_checksum);
    fr_checksum_value = (uint32_t) *fr_checksum;

    opcode = BLOCK_OP_WRFRME;
    reg = generate_register(opcode, 0, fr_checksum_value, 0);

    return_reg = block_io_bus(reg, temp_buf);

    uint64_t rt_temp = return_reg << 56;
    rt_temp = rt_temp >> 56;
    int8_t rt1 = rt_temp;

    if (rt1 == -1) {
	    return (-1);
    }

    while (rt1 == 2) {
	    // Invalid checksum
	    return_reg = block_io_bus(reg, temp_buf);

	    rt_temp = return_reg << 56;
	    rt_temp = rt_temp >> 56;
	    rt1 = rt_temp;
    }

    // The filesystem metadata for my data structures has been written to the block system!


    // Power off the filesystem
    opcode = BLOCK_OP_POWOFF;
    reg = generate_register(opcode, 0, 0, 0);
    return_reg = block_io_bus(reg, 0);

    return_reg = return_reg << 56;
    return_reg = return_reg >> 56;
    rt1 = return_reg;

    if (rt1 == -1) {
	return (-1);
    }

    free(temp_buf);
    temp_buf = NULL;
    free(fr_checksum);
    fr_checksum = NULL;
    
    // Close the block cache
    close_block_cache();
    free(cache_buf);

    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t block_open(char* path)
{
    int index;
    int dif;
    index = -1;

    // Go through each file and observe if file path exists
    for (int i = 0; i < num_files; i++) {
	    // Compare the path in the list vs. the specified path
	    dif = strcmp(all_files[i].path, (char *) path);
	    if (dif == 0) {
		    // We found the file!
		    index = i;
		    break;
	    }
    }

    if (index == -1) {
	    // This means we never found a matching file, so let's start a new file
	    index = num_files;
	    if (num_files > BLOCK_MAX_TOTAL_FILES) {
		    return (-1);
	    }

	    all_files = (struct file*) realloc(all_files, sizeof(struct file) * (num_files + 1));
	    
	    strcpy(all_files[index].path, path);

	    // Open the file
	    all_files[index].status = OPEN;

	    // Set length to 0
	    all_files[index].length = 0;

	    // Set seek position to 0
	    all_files[index].seek_pos = 0;
		    
	    // Establish a file handle
	    int32_t randomNum;
	    randomNum = getRandomValue(0, UINT32_MAX);
	    randomNum = randomNum >> 16;
	    int16_t fd = (int16_t) randomNum;
	    all_files[index].handle = fd;

	    // Assign a frame to this new file
	    all_files[index].frames = malloc(sizeof(uint16_t));

	    // Reference the number of frames used overall to determine which frame can be used to represent the beginning of this file
	    // If 0 frames have been used thus far, then this file can start from frame 0
	    all_files[index].frames[0] = num_frames_used + 1; // (num_frames_used + 1 b/c we need to reserve frame 0 for file metadata)
	    
	    all_files[index].num_frames = 1;
	    num_frames_used++;
	    num_files++;
    }
    
    // Return the file handle
    return (all_files[index].handle);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure

int16_t block_close(int16_t fd)
{
    int index;
    index = -1;

    // Go through each file and observe if the file handle exists
    for (int i = 0; i < num_files; i++) {
	    // Compare the path in the list vs. the specified path
	    if (fd == all_files[i].handle) {
		    // We found the file!
		    index = i;
		    break;
	    }
    }

    if (index == -1) {
	    // This means we never found a matching file, so return -1
	    return (-1);
    }
    else if (all_files[index].status == CLOSED) {
	    // This means the file was already closed
	    return (-1);
    }
    
    all_files[index].status = CLOSED;

    // Return successfully
    return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_read
// Description  : Reads "count" bytes from the file handle "fh" into the
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t block_read(int16_t fd, void* buf, int32_t count)
{
    // First check to see if the file exists
    int index;
    index = -1;
    // Go through each file and observe if the file handle exists
    for (int i = 0; i < num_files; i++) {
	    //  Compare the path in the list vs. the specified path
	    if (fd == all_files[i].handle) {
		    // We found the file!
		    index = i;
		    break;
	    }
    }

    if (index == -1) {
	    // This means we never found a matching file, so return -1
	    return (-1);
    }

    // Second, check to see if the file is open
    else if (all_files[index].status == CLOSED) {
	    // This means the file is not open
	    return (-1);
    }

    // Third determine how many bytes can be read, factoring in the end of the file
    uint32_t seek = all_files[index].seek_pos;
    uint32_t length = all_files[index].length;

    // If we will reach the end of the file within count bytes, then reduce count bytes so we don't go past the end of the file
    if (length - seek < count) {
	    count = length - seek;
    }

    // Fourth, determine which frame we need to read from, factoring in seek_pos
    int16_t frame_index;
    
    // Use floor division to figure out which frame we want to look at
    frame_index = seek / BLOCK_FRAME_SIZE;

    // Use modulo to figure out the seek position relative to the beginning of the frame
    seek = seek % BLOCK_FRAME_SIZE;

    // Now the seek position correctly positions us in the given frame
    // frame_index tells us the correct frame index to start looking at within all_files[index].frames

    // Keep track of the number of bytes left to write
    uint32_t count_remaining;
    count_remaining = count;

    // Make it easier to observe what the current frame we're reading from is
    uint16_t cur_frame;
    
    // Track how many bytes to read from current frame
    uint32_t bytes_to_read_in_cur_frame;
    bytes_to_read_in_cur_frame = 0;

    // Track how many bytes have been read so far for the file
    uint32_t bytes_so_far;
    bytes_so_far = 0;

    // The number of frames associated with the current file
    uint16_t num_frames;
    num_frames = all_files[index].num_frames;

    // Declare a variable to keep track of the checksum returned by the frame
    uint32_t frame_checksum;

    // Variable to hold the return code of block_io_bus
    int8_t rt;

    // Declare read opcode
    BlockOpCodes opcode = BLOCK_OP_RDFRME;

    // Create a register
    BlockXferRegister reg;

    // Create a variable to hold return reg
    BlockXferRegister return_reg;

    for (; frame_index < num_frames && count_remaining > 0; frame_index++) {
	    // Iterate through frames in the list, reading content from each frame at a time
	    cur_frame = all_files[index].frames[frame_index];
	    
	    // Attempt to read from the frame
	    cache_buf = get_block_cache(0, cur_frame);
	    if (cache_buf == NULL) {
		    // Generate register
		    reg = generate_register(opcode, cur_frame, 0, 0);

		    // Pass through io bus
		    return_reg = block_io_bus(reg, temp_buf);
	    
		    uint64_t rt_temp = return_reg << 56;
		    rt_temp = rt_temp >> 56;
		    rt = rt_temp;

		    if (rt == -1) {
			    return (-1);
		    }

		    // Observe the checksum of the frame returned by block_io_bus
		    return_reg = return_reg << 24;
		    return_reg = return_reg >> 32;
		    frame_checksum = return_reg;

		    // Calculate the checksum of the received framedata
		    compute_frame_checksum(temp_buf, fr_checksum);
		    fr_checksum_value = (uint32_t) *fr_checksum;
	    
		    while (frame_checksum != fr_checksum_value) {
			    // Checksums do not match! Call block_io_bus until they match
			    reg = generate_register(opcode, cur_frame, 0, 0);
			    return_reg = block_io_bus(reg, temp_buf);
		    
			    rt_temp = return_reg << 56;
			    rt_temp = rt_temp >> 56;
			    rt = rt_temp;

			    if (rt == -1) {
				    return (-1);
			    }

			    compute_frame_checksum(temp_buf, fr_checksum);
			    fr_checksum_value = (uint32_t) *fr_checksum;
		    }
	    }

	    // else, it exists in the cache and is already pointed to by cache_buf!

	    // If we are reading fewer bytes than the entire frame, adjust bytes_to_read_in_cur_frame to reflect that
	    if (count_remaining < BLOCK_FRAME_SIZE) {
		    bytes_to_read_in_cur_frame = count_remaining;
	    }

	    // Otherwise set bytes_to_read_in_cur_frame equal to the entire length of the frame
	    else {
		    bytes_to_read_in_cur_frame = BLOCK_FRAME_SIZE;
	    }

	    // Copy bytes_to_read_in_cur_frame bytes from read to the buf
	    if (cache_buf == NULL) {
	    	memcpy(buf + bytes_so_far, temp_buf+seek, bytes_to_read_in_cur_frame); // Copy bytes_to_read_in_cur_frame bytes from read to buf + offset
	    }
	    else {
		memcpy(buf + bytes_so_far, cache_buf + seek, bytes_to_read_in_cur_frame);
	    }

	    seek = 0;

	    bytes_so_far += bytes_to_read_in_cur_frame;
	    count_remaining -= bytes_to_read_in_cur_frame;
    }
    
    all_files[index].seek_pos += count;
    cache_buf = NULL;

    // Return successfully
    return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_write
// Description  : Writes "count" bytes to the file handle "fh" from the
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t block_write(int16_t fd, void* buf, int32_t count)
{
    // First check to see if the file exists
    int index;
    index = -1;
    //Go through eaach file and observe if the file handle exists
    for (int i = 0; i < num_files; i++) {
	    // Compare the path in the list vs. the specified path
	    if (fd == all_files[i].handle) {
		    // We found the file!
		    index = i;
		    break;
	    }
    }

    if (index == -1) {
	    // This means we never found a matching file, so return -1
	    return (-1);
    }

    // Second, check to see if the file is open
    else if (all_files[index].status == CLOSED) {
	    // This means the file is not open
	    return (-1);
    }

    // Third, determine if we need to allocate additional frames to accomodate for a larger file
    // Check to see if seek_pos + count > length
    uint32_t length;
    length = all_files[index].length;
    uint32_t seek;
    seek = all_files[index].seek_pos;

    if (seek + count > length) {
	    // We will need to adjust the length of the file
	    all_files[index].length = seek + count;
	    length = all_files[index].length;

   	    // Determine if we need to allocate additional frames
  	    while (length >= all_files[index].num_frames * BLOCK_FRAME_SIZE) {
    	    	// We take up more than num_frames frames, so assign a new frame to the file
  	    	all_files[index].num_frames++;
	    	all_files[index].frames = (uint16_t *) realloc(all_files[index].frames, sizeof(uint16_t) * all_files[index].num_frames);	

			// Example: if num_frames == 2, then metadata is in frame 0, this file takes up indices 1 and 2, so assign index 1 to a new frame 3
			all_files[index].frames[all_files[index].num_frames-1] = num_frames_used + 1;
		
			num_frames_used++;
			length -= BLOCK_FRAME_SIZE;
	    }
    }

    // Now that additional frames have been allocated, let's begin writing to a frames
    // Find the first frame that we need to write to, factoring in seek_pos
    uint16_t frame_index;
    
    // Floor division to get frame index
    frame_index = seek / BLOCK_FRAME_SIZE;

    // Modulo to get seek relative to start of frame
    seek = seek % BLOCK_FRAME_SIZE;

    // Variable that stores the current frame in the memory system that we want to look at
    uint16_t cur_frame;

    // We are now on the correct frame and at the correct seek position relative to the start of the frame

    // Track how many bytes are left to write
    uint32_t bytes_left_to_write;
    bytes_left_to_write = count;
    
    // Track how many bytes have been written so far
    uint32_t bytes_written;
    bytes_written = 0;

    // Create an opcode variable
    BlockOpCodes opcode;

    // Create register variable
    BlockXferRegister reg;

    // Create return register variable
    BlockXferRegister return_reg;

    // Create variable to hold the checksum returned by the io bus
    uint32_t frame_checksum;

    // Variable to hold the return code of return_reg
    int8_t rt;

    // Temporary register variable before obtaining rt
    uint64_t rt_temp;

    // Begin a loop that continues as long as we want to continue writing bytes
    while (bytes_left_to_write > 0) {
	    cur_frame = all_files[index].frames[frame_index];

	    // There are three different scenarios when writing
	    // 1. We are writing in the middle of a frame and preserving the beginning
	    // 2. We are writing an entire frame
	    // 3. We are writing the beginning of a frame and preserving the end

	    // 1. We are writing in the middle
	    if (seek > 0) {
	    	// First read the frame
		// Attempt to read from cache
		cache_buf = get_block_cache(0, cur_frame);

		if (cache_buf == NULL) {
			// Set read opcode
			opcode = BLOCK_OP_RDFRME;

			// Generate register
			reg = generate_register(opcode, cur_frame, 0, 0);

			// Pass along io bus
			return_reg = block_io_bus(reg, temp_buf);

			rt_temp = return_reg << 56;
			rt_temp = rt_temp >> 56;
			rt = rt_temp;

			if (rt == -1) {
			    return (-1);
			}

			// Calculate checksum of returned data
			return_reg = return_reg << 24;
			return_reg = return_reg >> 32;
			frame_checksum = return_reg;

			// Calculate the checksum of the received framedata
			compute_frame_checksum(temp_buf, fr_checksum);
			fr_checksum_value = (uint32_t) *fr_checksum;

			while (frame_checksum != fr_checksum_value) {
				// Checksums do not match! Call block_io_bus until they match
				reg = generate_register(opcode, cur_frame, 0, 0);
				return_reg = block_io_bus(reg, temp_buf);
		    
				rt_temp = return_reg << 56;
				rt_temp = rt_temp >> 56;
				rt = rt_temp;

				if (rt == -1) {
					return (-1);
				}

				compute_frame_checksum(temp_buf, fr_checksum);
				fr_checksum_value = (uint32_t) *fr_checksum;
			}
		}

	     	// if we are not overwriting the rest of the frame, then just memcpy
	        if (bytes_left_to_write <= BLOCK_FRAME_SIZE - seek) {
			if (cache_buf == NULL) {
		      		memcpy(temp_buf + seek, buf, bytes_left_to_write);
			}
			else {
				memcpy(temp_buf + seek, cache_buf, bytes_left_to_write);
			}

		        bytes_written = bytes_left_to_write;
			bytes_left_to_write = 0;
	        }

	        // else we're writing to the end of the current frame
	        else {
			if (cache_buf == NULL) {
		        	memcpy(temp_buf + seek, buf, BLOCK_FRAME_SIZE - seek);
			}
			else {
				memcpy(temp_buf + seek, cache_buf, BLOCK_FRAME_SIZE - seek);
			}

			bytes_written += (BLOCK_FRAME_SIZE - seek);
		        bytes_left_to_write -= (BLOCK_FRAME_SIZE - seek);
	        }
	    
	    // Since we'll be either moving onto a new frame or ending, reset seek position
	    seek = 0;

	    }

	    // 2. We are writing an entire frame
	    else if (bytes_left_to_write >= BLOCK_FRAME_SIZE) {
		    memcpy(temp_buf, buf + bytes_written, BLOCK_FRAME_SIZE);
		    bytes_written += BLOCK_FRAME_SIZE;
		    bytes_left_to_write -= BLOCK_FRAME_SIZE;
	    }

	    // 3. We are writing to the beginning of a frame but not the whole frame
	    else {
		    // Read the frame
		    // Attempt to read from cache
		    cache_buf = get_block_cache(0, cur_frame);
		    if (cache_buf == NULL) {
			    // Set read opcode
			    opcode = BLOCK_OP_RDFRME;

			    // Generate register
			    reg = generate_register(opcode, cur_frame, 0, 0);

			    // Pass along io bus
			    return_reg = block_io_bus(reg, temp_buf);

			    rt_temp = return_reg << 56;
			    rt_temp = rt_temp >> 56;
			    rt = rt_temp;

			    if (rt == -1) {
			        return (-1);
			    }

			    // Calculate checksum of returned data
			    return_reg = return_reg << 24;
			    return_reg = return_reg >> 32;
			    frame_checksum = return_reg;

			    // Calculate the checksum of the received framedata
			    compute_frame_checksum(temp_buf, fr_checksum);
			    fr_checksum_value = (uint32_t) *fr_checksum;

			    while (frame_checksum != fr_checksum_value) {
				    // Checksums do not match! Call block_io_bus until they match
				    reg = generate_register(opcode, cur_frame, 0, 0);
				    return_reg = block_io_bus(reg, temp_buf);
		    
				    rt_temp = return_reg << 56;
				    rt_temp = rt_temp >> 56;
				    rt = rt_temp;

				    if (rt == -1) {
				    	return (-1);
				    }

				    compute_frame_checksum(temp_buf, fr_checksum);
				    fr_checksum_value = (uint32_t) *fr_checksum;
			    }
		    }
		    if (cache_buf == NULL) {
		    	memcpy(temp_buf, buf + bytes_written, bytes_left_to_write);
		    }
		    else {
		    	memcpy(temp_buf, cache_buf + bytes_written, bytes_left_to_write);
		    }

		    bytes_written += bytes_left_to_write;
		    bytes_left_to_write = 0;
	    }

	    // In each case, temp_buf is now populated with the data that we want to write with
	    // Calculate the checksum to submit to the BLOCK system
	    compute_frame_checksum(temp_buf, fr_checksum);
	    fr_checksum_value = (uint32_t) *fr_checksum;

	    // Specify write opcode
	    opcode = BLOCK_OP_WRFRME;

	    // Generate register
	    reg = generate_register(opcode, cur_frame, fr_checksum_value, 0);

	    return_reg = block_io_bus(reg, temp_buf);

	    rt_temp = return_reg << 56;
	    rt_temp = rt_temp >> 56;
	    rt = rt_temp;

	    if (rt == -1) {
		    return (-1);
	    }

	    while (rt == 2) {
		    // Invalid checksum
		    return_reg = block_io_bus(reg, temp_buf);

		    rt_temp = return_reg << 56;
		    rt_temp = rt_temp >> 56;
		    rt = rt_temp;
	    }
	    frame_index++;

	    // Write to the cache
	    put_block_cache(0, cur_frame, temp_buf);
    }

    cache_buf = NULL;
    all_files[index].seek_pos += count;
   // Return successfully
   return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : block_seek
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure

int32_t block_seek(int16_t fd, uint32_t loc)
{
    // First, check to see if the file exists
    int index;
    index = -1;
    // Go through each file and observe if the file handle exists
    for (int i = 0; i < num_files; i++) {
	    // Compare the path in the list vs. the specified path
	    if (fd == all_files[i].handle) {
		    // We found the file!
		    index = i;
		    break;
	    }
    }

    if (index == -1) {
	    // This means we never found a matching file, so return -1
	    return (-1);
    }

    // Second, check to see if the file is open
    else if (all_files[index].status == CLOSED) {
	    // This means the file is not open
	    return (-1);
    }

    // Third, set the seek position to loc
    // Check to make sure loc <= length
    // (Say file is 24 bytes. To seek to end of file you can place it after the 24th byte aka index 25)
    if (loc <= all_files[index].length) {
    	   all_files[index].seek_pos = loc;
    }
    else {
	  return (-1);
    }

    // Return successfully
    return (0);
}
