#ifndef BLOCK_DRIVER_INCLUDED
#define BLOCK_DRIVER_INCLUDED

////////////////////////////////////////////////////////////////////////////////
//
//  File           : block_driver.h
//  Description    : This is the header file for the standardized IO functions
//                   for used to access the BLOCK storage system.
//
//  Author         : Patrick McDaniel
//

// Include files
#include <stdint.h>

// Defines
#define BLOCK_MAX_TOTAL_FILES 1024 // Maximum number of files ever
#define BLOCK_MAX_PATH_LENGTH 128 // Maximum length of filename length

struct file {
	char path[BLOCK_MAX_PATH_LENGTH];
	int16_t handle;
	uint32_t length;
	enum status {
		OPEN = 1,
		CLOSED = 0
	}status;
	uint32_t seek_pos;

	// To avoid excessive memory consumption, point to an area of memory to store an array of frame numbers
	uint16_t *frames;
	uint16_t num_frames;
}file;

//
// Interface functions

int compute_frame_checksum(void* frame, uint32_t* checksum);
// Generate checksum for frame

uint64_t generate_register(uint8_t kr1, uint16_t fm1, uint32_t cs1, uint8_t rt1);
// Generate register based off of kr1, fm1, cs1, rt1

int32_t block_poweron(void);
// Startup up the BLOCK interface, initialize filesystem

int32_t block_poweroff(void);
// Shut down the BLOCK interface, close all files

int16_t block_open(char* path);
// This function opens the file and returns a file handle

int16_t block_close(int16_t fd);
// This function closes the file

int32_t block_read(int16_t fd, void* buf, int32_t count);
// Reads "count" bytes from the file handle "fh" into the buffer  "buf"

int32_t block_write(int16_t fd, void* buf, int32_t count);
// Writes "count" bytes to the file handle "fh" from the buffer  "buf"

int32_t block_seek(int16_t fd, uint32_t loc);
// Seek to specific point in the file

#endif
