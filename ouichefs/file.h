#ifndef INCLUDE_OUICHEFS_FILE_H_
#define INCLUDE_OUICHEFS_FILE_H_

#include "linux/types.h"

struct ouichefs_extent {
	uint32_t start; /* first physical block number of the run */
	uint32_t count; /* number of consecutive blocks in the run */
};

#define OUICHEFS_MAX_EXTENTS = OUICHEFS_BLOCK_SIZE / sizeof(struct ouichefs_extent)

#endif // INCLUDE_OUICHEFS_FILE_H_
