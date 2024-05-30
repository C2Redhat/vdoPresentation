// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "albtest.h"
#include "assertions.h"
#include "config.h"
#include "hash-utils.h"
#include "index.h"
#include "string-utils.h"
#include "testPrototypes.h"
#include "testRequests.h"

static struct uds_index    *theIndex;
static struct block_device *testDevice;

/**********************************************************************/
static void init(void)
{
  initialize_test_requests();
}

/**********************************************************************/
static void deinit(void)
{
  uninitialize_test_requests();
}

/**********************************************************************/
static void createIndex(unsigned int zone_count)
{
  struct uds_parameters params = {
    .memory_size = UDS_MEMORY_CONFIG_256MB,
    .bdev = getTestBlockDevice(),
    .zone_count = zone_count,
  };
  struct uds_configuration *config;
  UDS_ASSERT_SUCCESS(uds_make_configuration(&params, &config));
  UDS_ASSERT_SUCCESS(uds_make_index(config, UDS_CREATE, NULL, NULL, &theIndex));
  testDevice = params.bdev;
  uds_free_configuration(config);
}

/**********************************************************************/
static void requestIndex(struct uds_record_name *name,
                         struct uds_record_data *data)
{
  unsigned int zone = uds_get_volume_index_zone(theIndex->volume_index, name);
  struct uds_request request = {
    .record_name  = *name,
    .new_metadata = *data,
    .zone_number  = zone,
    .type         = UDS_POST,
  };
  submit_test_request(theIndex, &request);
}

/**********************************************************************/
static void stressZonesTest(void)
{
  struct uds_record_name orig;
  struct uds_record_name name;
  struct uds_record_data data;

  createIndex(2);
  CU_ASSERT_EQUAL(theIndex->newest_virtual_chapter, 0);

  createRandomBlockName(&orig);
  unsigned int initialZone = uds_get_volume_index_zone(theIndex->volume_index, &orig);

  do {
    createRandomBlockNameInZone(theIndex, initialZone, &name);
    createRandomMetadata(&data);
    requestIndex(&name, &data);
  } while (theIndex->newest_virtual_chapter < 4);

  uds_free_index(theIndex);
  putTestBlockDevice(testDevice);
}

/**********************************************************************/
static void stressChapterIndexBytesTest(void)
{
  struct uds_record_name orig;
  struct uds_record_name name;
  struct uds_record_data data;
  unsigned int zone;
  createIndex(0);

  uint64_t chapter = theIndex->newest_virtual_chapter;
  CU_ASSERT_EQUAL(chapter, 0);

  createRandomBlockName(&orig);
  uint32_t chapterIndexField = uds_extract_chapter_index_bytes(&orig);
  do {
    createRandomBlockName(&name);
    set_chapter_index_bytes(&name, chapterIndexField);
    zone = uds_get_volume_index_zone(theIndex->volume_index, &name);
    createRandomMetadata(&data);
    requestIndex(&name, &data);
  } while (theIndex->zones[zone]->newest_virtual_chapter == chapter);

  uds_free_index(theIndex);
  putTestBlockDevice(testDevice);
}

/**********************************************************************/
static void stressVolumeIndexBytesTest(void)
{
  struct uds_record_name orig, name;
  struct uds_record_data data;
  struct volume_index_stats stats;

  createIndex(0);
  createRandomBlockName(&orig);
  do {
    createCollidingBlock(&orig, &name);
    createRandomMetadata(&data);
    requestIndex(&name, &data);
    uds_get_volume_index_stats(theIndex->volume_index, &stats);
  } while (stats.overflow_count < 1);

  uds_free_index(theIndex);
  putTestBlockDevice(testDevice);
}

/**********************************************************************/
static const CU_TestInfo stressTests[] = {
  {"Stress Volume Index",  stressVolumeIndexBytesTest },
  {"Stress Chapter Index", stressChapterIndexBytesTest },
  {"Stress Zones",         stressZonesTest },
  CU_TEST_INFO_NULL,
};

static const CU_SuiteInfo suite = {
  .name        = "IndexStress_n1",
  .initializer = init,
  .cleaner     = deinit,
  .tests       = stressTests,
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
