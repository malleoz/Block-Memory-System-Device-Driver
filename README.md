# Block Memory System Device Driver
A basic device driver utilizing a block memory system.

*Created for an Intro to Systems Programming class.*

This device driver sits between a virtual application and virtualized hardware. This drive makes use of a block memory system. The application, provided by the instructor, implements the basic UNIX file operations: open, close, read, write, and seek.

For this assignment, I had to implement the following block memory system functions: block_poweron, block_poweroff, block_open, block_close, block_read, block_write, and block_seek. In addition, I also had to allow for persistent block storage by storing the current block system data to a file and being able to read from the file at a later point. Lastly, I also implemented a block cache system to prevent unecessary reads from the memory system.


To run the block system with a workload file, use:
* ./block_sim -v workload/assign2-workload.txt

To run the block system with an implemented cache, use:
* ./block_sim -v -c <cache_size> workload/assign4-workload.txt
