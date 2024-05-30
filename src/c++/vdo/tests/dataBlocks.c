/*
 * %COPYRIGHT%
 *
 * %LICENSE%
 *
 * $Id$
 */

#include "dataBlocks.h"

#include "memory-alloc.h"
#include "numeric.h"
#include "syscalls.h"

#include "constants.h"
#include "int-map.h"

#include "vdoAsserts.h"

enum {
  UINT64s_PER_BLOCK = VDO_BLOCK_SIZE / sizeof(uint64_t),
  INITIAL_BLOCKS    = 64,
};

static DataFormatter  *dataFormatter;
static struct int_map *dataBlocks  = NULL;
static char           *buffer;
static block_count_t   maxIndex    = 0;
static bool            initialized = false;

/**
 * Fill a block with an 8 byte value.
 *
 * @param block   The block to format
 * @param value   The value with which to format the block
 **/
static void fillWithValue(char *block, uint64_t value)
{
  uint64_t *b = (uint64_t *) block;
  for (size_t i = 0; i < UINT64s_PER_BLOCK; i++) {
    b[i] = value;
  }
}

/**********************************************************************/
void fillWithOffset(char *block, block_count_t index)
{
  fillWithValue(block, index);
}

/**********************************************************************/
void fillWithOffsetPlusOne(char *block, block_count_t index)
{
  fillWithValue(block, index + 1);
}

/**********************************************************************/
void fillWithFortySeven(char          *block,
                        block_count_t  index __attribute__((unused)))
{
  fillWithValue(block, 47);
}

/**********************************************************************/
void fillAlternating(char *block, block_count_t index)
{
  fillWithValue(block, (index % 2) + 1);
}

/**********************************************************************/
void initializeDataBlocks(DataFormatter *formatter)
{
  CU_ASSERT_FALSE(initialized);
  CU_ASSERT_EQUAL(VDO_BLOCK_SIZE % sizeof(uint64_t), 0);
  dataFormatter = formatter;
  buffer        = NULL;
  maxIndex      = 0;
  initialized   = true;
}

/**********************************************************************/
void tearDownDataBlocks(void)
{
  if (dataBlocks != NULL) {
    for (block_count_t i = 0; i < maxIndex; i++) {
      vdo_free(vdo_int_map_remove(dataBlocks, i));
    }
    vdo_int_map_free(vdo_forget(dataBlocks));
  }

  vdo_free(buffer);
  buffer        = NULL;
  maxIndex      = 0;
  dataFormatter = NULL;
  initialized   = false;
}

/**********************************************************************/
char *getDataBlock(block_count_t index)
{
  if (maxIndex == 0) {
    VDO_ASSERT_SUCCESS(vdo_int_map_create(INITIAL_BLOCKS, &dataBlocks));
  }

  if (buffer == NULL) {
    VDO_ASSERT_SUCCESS(vdo_allocate(VDO_BLOCK_SIZE, char, __func__, &buffer));
  }

  char *block;
  VDO_ASSERT_SUCCESS(vdo_int_map_put(dataBlocks,
                                     index,
                                     buffer,
                                     false,
                                     (void **) &block));
  if (block == NULL) {
    block = buffer;
    buffer = NULL;
    dataFormatter(block, index);
    maxIndex = max(maxIndex, index + 1);
  }

  return block;
}
