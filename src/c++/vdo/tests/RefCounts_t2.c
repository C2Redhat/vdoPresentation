/*
 * %COPYRIGHT%
 *
 * %LICENSE%
 *
 * $Id$
 */

#include "albtest.h"

#include <stdlib.h>

#include "assertions.h"
#include "memory-alloc.h"
#include "syscalls.h"
#include "time-utils.h"

#include "data-vio.h"
#include "slab-depot.h"
#include "status-codes.h"

#include "blockAllocatorUtils.h"
#include "vdoAsserts.h"
#include "vdoTestBase.h"

enum {
  SLAB_SIZE    = (1 << 23),
  COUNT        = 100000,
  JOURNAL_SIZE = 2,
};

struct vdo_slab *slab;

/**********************************************************************/
static void initializeRefCounts(void)
{
  TestParameters parameters = {
    .slabCount = 1,
    .slabSize = SLAB_SIZE,
  };
  initializeVDOTest(&parameters);
  srand(42);

  slab = vdo->depot->slabs[0];

  /*
   * Set the slab to be unrecovered so that slab journal locks will be ignored.
   * Since this test doesn't maintain the correct lock invariants, it would
   * fail on a lock count underflow otherwise.
   */
  slab->status = VDO_SLAB_REQUIRES_SCRUBBING;
}

/**********************************************************************/
static void tearDownRefCounts(void)
{
  // Put the vdo in read-only mode so it doesn't try to write out all the reference count blocks.
  forceVDOReadOnlyMode();
  tearDownVDOTest();
}

/**
 * Set a PBN to have a given number of references.
 *
 * @param pbn   The physical block number to modify
 * @param value The reference count to give the block
 **/
static void setReferenceCount(physical_block_number_t pbn, size_t value)
{
  enum reference_status      refStatus;

  pbn += slab->start;
  struct reference_updater updater = {
    .operation = VDO_JOURNAL_DATA_REMAPPING,
    .increment = false,
    .zpbn      = {
      .pbn = pbn,
    },
  };
  VDO_ASSERT_SUCCESS(getReferenceStatus(slab, pbn, &refStatus));
  while (refStatus == RS_SHARED) {
    VDO_ASSERT_SUCCESS(adjust_reference_count(slab, &updater, NULL));
    VDO_ASSERT_SUCCESS(getReferenceStatus(slab, pbn, &refStatus));
  }
  if (refStatus == RS_SINGLE) {
    VDO_ASSERT_SUCCESS(adjust_reference_count(slab, &updater, NULL));
    VDO_ASSERT_SUCCESS(getReferenceStatus(slab, pbn, &refStatus));
  }
  CU_ASSERT_EQUAL(refStatus, RS_FREE);
  updater.increment = true;
  for (size_t i = 0; i < value; i++) {
    VDO_ASSERT_SUCCESS(adjust_reference_count(slab, &updater, NULL));
  }
}

/**
 * Time the amount of time it takes to find blocks, and clean up.
 **/
static void performanceTest(block_count_t blocks)
{
  block_count_t freeBlocks = countUnreferencedBlocks(slab, 0, blocks);
  uint64_t   elapsed    = current_time_us();
  for (block_count_t i = 0; i < freeBlocks; i++) {
    physical_block_number_t pbn;
    VDO_ASSERT_SUCCESS(allocate_slab_block(slab, &pbn));
    CU_ASSERT_TRUE(pbn < blocks);
  }

  elapsed = current_time_us() - elapsed;
  printf("(%lu free in %lu usec) ", freeBlocks, elapsed);

  CU_ASSERT_EQUAL(0, countUnreferencedBlocks(slab, 0, blocks));
}

/**
 * Allocate a 100000-element empty refcount array.
 **/
static void testEmptyArray(void)
{
  performanceTest(COUNT);
}

/**
 * Allocate a 100000-element refcount array, assign random values, then time
 * finding free blocks.
 **/
static void testVeryFullArray(void)
{
  for (size_t k = 0; k < COUNT; k++) {
    setReferenceCount(k, random() % 16);
  }
  performanceTest(COUNT);
}

/**
 * Allocate a 100000-element refcount array, and make it 90% free space.
 **/
static void testMostlyEmptyArray(void)
{
  for (size_t k = 0; k < COUNT / 10; k++) {
    size_t index = random() % COUNT;
    setReferenceCount(index, random() % 16);
  }
  performanceTest(COUNT);
}

/**
 * Allocate a 100000-element refcount array and make it 90% used space.
 **/
static void testMostlyFullArray(void)
{
  for (size_t k = 0; k < COUNT; k++) {
    setReferenceCount(k, random() % 16);
  }
  for (size_t k = 0; k < COUNT / 10; k++) {
    size_t index = random() % COUNT;
    setReferenceCount(index, 0);
  }
  performanceTest(COUNT);
}

/**
 * Test a full slab except for the last block.
 **/
static void testFullArray(void)
{
  // Incref all blocks except the last.
  block_count_t dataBlocks = vdo->depot->slab_config.data_blocks;
  for (size_t k = 1; k < dataBlocks - 1; k++) {
    setReferenceCount(k, 1);
  }
  performanceTest(dataBlocks);
}

/**
 * Test all free block positions are found correctly for a given refcount
 * array length.
 *
 * @param length        The refcount array length to test
 **/
static void testAllFreeBlockPositions(block_count_t arrayLength)
{
  // Make all counts 1.
  for (size_t k = 0; k < arrayLength; k++) {
    setReferenceCount(k, 1);
  }

  for (physical_block_number_t freePBN = 1; freePBN < arrayLength; freePBN++) {
    // Adjust the previously-free block to 1, and the new free one to 0.
    freePBN += slab->start;
    struct reference_updater updater = {
      .operation = VDO_JOURNAL_DATA_REMAPPING,
      .increment = true,
      .zpbn = {
        .pbn = freePBN - 1,
      },
    };
    VDO_ASSERT_SUCCESS(adjust_reference_count(slab, &updater, NULL));
    updater.increment = false;
    updater.zpbn.pbn  = freePBN;
    VDO_ASSERT_SUCCESS(adjust_reference_count(slab, &updater, NULL));

    // Test that the free block is found correctly for all starts and ends.
    for (size_t start = 0; start < arrayLength; start++) {
      slab->search_cursor.index = start;
      for (size_t end = start; end <= arrayLength; end++) {
        bool inRange = ((start <= (freePBN - slab->start)) && ((freePBN - slab->start) < end));
        slab_block_number freeIndex;
        slab->search_cursor.end_index = end;
        if (find_free_block(slab, &freeIndex)) {
          CU_ASSERT_TRUE(inRange);
          CU_ASSERT_EQUAL(freePBN, slab->start + freeIndex);
        } else {
          CU_ASSERT_FALSE(inRange);
        }
      }
    }
  }
}

/**
 * The octet code kicks in at 32 refcounts. Test all possible single
 * free block locations for refcount arrays of length 32 to 96, to ensure all
 * reasonable corner cases of the octet code are caught.
 **/
static void testAllSmallArrays(void)
{
  for (size_t size = 32; size < 96; size++) {
    testAllFreeBlockPositions(size);
  }
}

/**********************************************************************/
static CU_TestInfo tests[] = {
  { "0% full array",           testEmptyArray      },
  { "10% full array",          testMostlyEmptyArray},
  { "90% full array",          testMostlyFullArray },
  { "99.6% full array",        testVeryFullArray   },
  { "100% full slab",          testFullArray       },
  { "all small arrays",        testAllSmallArrays  },
  CU_TEST_INFO_NULL,
};

static CU_SuiteInfo suite = {
  .name                   = "Reference counter speed tests (RefCounts_t2)",
  .initializer            = initializeRefCounts,
  .cleaner                = tearDownRefCounts,
  .tests                  = tests,
};

CU_SuiteInfo *initializeModule(void)
{
  return &suite;
}
