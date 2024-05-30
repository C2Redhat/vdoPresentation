/*
 * %COPYRIGHT%
 *
 * %LICENSE%
 *
 * $Id$
 */

/**
 * Test rebuild after saving the index with a partial chapter 0, doing
 * convert_to_lvm and then crashing after writing a full chapters.
 **/

#include "albtest.h"
#include "assertions.h"
#include "blockTestUtils.h"
#include "convertToLVM.h"
#include "dory.h"
#include "hash-utils.h"
#include "indexer.h"
#include "oldInterfaces.h"
#include "testPrototypes.h"

enum {
  NUM_CHUNKS = 1000,
};

static struct block_device *testDevice;

/**********************************************************************/
static void postChunks(struct uds_index_session *indexSession,
                       int                       base,
                       int                       count)
{
  long index;
  for (index = base; index < base + count; index++) {
    struct uds_record_name chunkName = hash_record_name(&index, sizeof(index));
    oldPostBlockName(indexSession, NULL, (struct uds_record_data *) &chunkName,
                     &chunkName, cbStatus);
  }
  UDS_ASSERT_SUCCESS(uds_flush_index_session(indexSession));
}

/**********************************************************************/
static void fullRebuildTest(void)
{
  initializeOldInterfaces(2000);

  // Create a new index.
  struct uds_parameters params = {
    .memory_size = UDS_MEMORY_CONFIG_256MB,
    .bdev = testDevice,
  };
  randomizeUdsNonce(&params);
  struct uds_index_session *indexSession;
  UDS_ASSERT_SUCCESS(uds_create_index_session(&indexSession));
  UDS_ASSERT_SUCCESS(uds_open_index(UDS_CREATE, &params, indexSession));
  // Write the base set of 1000 chunks to the index.
  postChunks(indexSession, 0, NUM_CHUNKS);

  // Write one chapter so convert will have something to work with.
  unsigned int blocksPerChapter = getBlocksPerChapter(indexSession);
  CU_ASSERT(NUM_CHUNKS < blocksPerChapter);
  postChunks(indexSession, NUM_CHUNKS, blocksPerChapter);
  UDS_ASSERT_SUCCESS(uds_close_index(indexSession));

  // Do the LVM conversion
  off_t moved;
  UDS_ASSERT_SUCCESS(uds_convert_to_lvm(&params, 0, &moved));
                                        
  struct uds_parameters params2 = {
    .memory_size = params.memory_size,
    .bdev = testDevice,
    .nonce = params.nonce,
    .offset = moved,
  };

  // Open the converted index.
  UDS_ASSERT_SUCCESS(uds_open_index(UDS_NO_REBUILD, &params2, indexSession));

  // Write another chapter so close will have to save.
  postChunks(indexSession, NUM_CHUNKS + (2 * blocksPerChapter),
             blocksPerChapter);

  // Turn off writing, and do a dirty closing of the index.
  set_dory_forgetful(true);
  UDS_ASSERT_ERROR(-EROFS, uds_close_index(indexSession));
  set_dory_forgetful(false);

  // Make sure the index will not load.
  UDS_ASSERT_ERROR(-EEXIST,
                   uds_open_index(UDS_NO_REBUILD, &params2, indexSession));
  // Rebuild the index.
  UDS_ASSERT_SUCCESS(uds_open_index(UDS_LOAD, &params2, indexSession));
  // Repost the base set of 1000 chunks to verify that they are still there.
  postChunks(indexSession, 0, NUM_CHUNKS);
  UDS_ASSERT_SUCCESS(uds_flush_index_session(indexSession));
  struct uds_index_stats indexStats;
  UDS_ASSERT_SUCCESS(uds_get_index_session_stats(indexSession, &indexStats));
  CU_ASSERT_EQUAL(NUM_CHUNKS, indexStats.posts_found);
  CU_ASSERT_EQUAL(0, indexStats.posts_not_found);
  UDS_ASSERT_SUCCESS(uds_close_index(indexSession));
  UDS_ASSERT_SUCCESS(uds_destroy_index_session(indexSession));
  uninitializeOldInterfaces();
}

/**********************************************************************/
static void initializerWithBlockDevice(struct block_device *bdev)
{
  testDevice = bdev;
}

/**********************************************************************/

static const CU_TestInfo tests[] = {
  {"Rebuild Converted Index", fullRebuildTest },
  CU_TEST_INFO_NULL,
};

static const CU_SuiteInfo suite = {
  .name                       = "RebuildConverted_n1",
  .initializerWithBlockDevice = initializerWithBlockDevice,
  .tests                      = tests,
};

/**
 * Entry point required by the module loader.
 *
 * @return      a pointer to the const CU_SuiteInfo structure.
 **/
const CU_SuiteInfo *initializeModule(void)
{
  return &suite;
}
