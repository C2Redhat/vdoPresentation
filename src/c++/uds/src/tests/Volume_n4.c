// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "albtest.h"
#include "assertions.h"
#include "config.h"
#include "hash-utils.h"
#include "memory-alloc.h"
#include "numeric.h"
#include "random.h"
#include "string-utils.h"
#include "testPrototypes.h"
#include "volume.h"
#include "volumeUtils.h"

enum {
  // Matches the value in volume.c
  VOLUME_CACHE_MAX_QUEUED_READS = 4096,
};

typedef struct readRequest {
  struct uds_request request;
  uint32_t           physicalPage;
} ReadRequest;

static struct uds_configuration *config;
static struct index_geometry    *geometry;
static struct index_layout      *layout;
static struct volume            *volume;
static struct block_device      *testDevice;
static unsigned int              numRequestsQueued = 0;
static struct mutex              numRequestsMutex;
static struct cond_var           allDoneCond;
static bool                      keepRunning       = false;

/**********************************************************************/
static void freeReadRequest(struct uds_request *request)
{
  // Release the counted reference to the context that was acquired for the
  // request (and not released) in createRequest().
  ReadRequest *readRequest = container_of(request, ReadRequest, request);
  vdo_free(readRequest);
}

/**********************************************************************/
static void verifyPageData(uint32_t physicalPage, struct cached_page *cp, size_t length)
{
  UDS_ASSERT_EQUAL_BYTES(test_pages[physicalPage], dm_bufio_get_block_data(cp->buffer), length);
}

/**********************************************************************/
static void retryReadRequest(struct uds_request *request)
{
  freeReadRequest(request);
  mutex_lock(&numRequestsMutex);
  if (--numRequestsQueued == 0) {
    uds_broadcast_cond(&allDoneCond);
  }
  mutex_unlock(&numRequestsMutex);
}

/**********************************************************************/
static void retryReadRequestAndVerify(struct uds_request *request)
{
  ReadRequest *readRequest = container_of(request, ReadRequest, request);
  uint32_t physicalPage = readRequest->physicalPage;

  struct cached_page *actual;
  // Make sure the page read is synchronous. We do not need to grab
  // the volume read lock here, because the caller of this function already
  // has it.
  UDS_ASSERT_SUCCESS(get_volume_page_locked(volume, physicalPage, &actual));
  verifyPageData(physicalPage, actual, geometry->bytes_per_page);
  retryReadRequest(request);
}

/**********************************************************************/
static void init(request_restarter_fn restartRequest, unsigned int zoneCount)
{
  set_request_restarter(restartRequest);

  mutex_init(&numRequestsMutex);
  uds_init_cond(&allDoneCond);
  numRequestsQueued = 0;
  testDevice = getTestBlockDevice();

  struct uds_parameters params = {
    .memory_size = UDS_MEMORY_CONFIG_256MB,
    .bdev = testDevice,
    .zone_count = zoneCount,
  };
  UDS_ASSERT_SUCCESS(uds_make_configuration(&params, &config));
  resizeDenseConfiguration(config, 4096, 16, 256);
  UDS_ASSERT_SUCCESS(uds_make_index_layout(config, true, &layout));

  UDS_ASSERT_SUCCESS(uds_make_volume(config, layout, &volume));

  geometry = config->geometry;
  makePageArray(geometry->pages_per_volume, geometry->bytes_per_page);
  writeTestVolumeData(volume, geometry);
}

/**********************************************************************/
static void deinit(void)
{
  set_request_restarter(NULL);
  freePageArray();
  uds_free_volume(volume);
  uds_free_configuration(config);
  uds_free_index_layout(vdo_forget(layout));
  putTestBlockDevice(testDevice);
#ifndef __KERNEL__
  uds_destroy_cond(&allDoneCond);
#endif  /* not __KERNEL__ */
  mutex_destroy(&numRequestsMutex);
}

/**********************************************************************/
static void computeNameOnPage(struct uds_record_name *name, uint32_t physicalPage)
{
  struct delta_index_page indexPage;
  uint32_t pageInChapter = (physicalPage - HEADER_PAGES_PER_VOLUME) % geometry->pages_per_chapter;
  if (pageInChapter >= geometry->index_pages_per_chapter) {
    /* This is a record page so it doesn't matter what record name we use. */
    return;
  }

  UDS_ASSERT_SUCCESS(uds_initialize_chapter_index_page(&indexPage,
                                                       geometry,
                                                       test_pages[physicalPage],
                                                       volume->nonce));
  u32 listNumber = uds_hash_to_chapter_delta_list(name, geometry);
  while ((listNumber < indexPage.lowest_list_number) ||
         (listNumber > indexPage.highest_list_number)) {
    createRandomBlockName(name);
    listNumber = uds_hash_to_chapter_delta_list(name, geometry);
  }
}

/**********************************************************************/
static struct uds_request *newReadRequest(uint32_t physicalPage)
{
  ReadRequest *readRequest = NULL;

  UDS_ASSERT_SUCCESS(vdo_allocate(1, ReadRequest, __func__, &readRequest));
  readRequest->physicalPage = physicalPage;
  readRequest->request.unbatched = true;
  computeNameOnPage(&readRequest->request.record_name, physicalPage);
  return &readRequest->request;
}

/**********************************************************************/
static bool waitCondTimeout(struct cond_var *cond, struct mutex *mutex, ktime_t timeout)
{
  int result = uds_timed_wait_cond(cond, mutex, timeout);
  return (result != ETIMEDOUT);
}

/**********************************************************************/
static void testSequentialGet(void)
{
  init(retryReadRequestAndVerify, 1);
  unsigned int chapter, page;
  for (chapter = 0; chapter < geometry->chapters_per_volume; ++chapter) {
    for (page = 0; page < geometry->pages_per_chapter; ++page) {
      u32 physicalPage = map_to_physical_page(geometry, chapter, page);
      struct uds_request *request = newReadRequest(physicalPage);
      struct cached_page *actual;
      begin_pending_search(&volume->page_cache, physicalPage, 0);
      // Make sure the page read is asynchronous
      int result = get_volume_page_protected(volume, request, physicalPage, &actual);
      if (result == UDS_SUCCESS) {
        freeReadRequest(request);
        verifyPageData(physicalPage, actual, geometry->bytes_per_page);
      } else if (result == UDS_QUEUED) {
        mutex_lock(&numRequestsMutex);
        ++numRequestsQueued;
        mutex_unlock(&numRequestsMutex);
      }
      end_pending_search(&volume->page_cache, 0);
    }
  }
  mutex_lock(&numRequestsMutex);
  while (numRequestsQueued > 0) {
    CU_ASSERT_TRUE(waitCondTimeout(&allDoneCond, &numRequestsMutex, seconds_to_ktime(10)));
  }
  mutex_unlock(&numRequestsMutex);
}

/**********************************************************************/
static void testStumblingGet(void)
{
  init(retryReadRequestAndVerify, 1);
  unsigned int page = HEADER_PAGES_PER_VOLUME;
  while (page < geometry->pages_per_volume + HEADER_PAGES_PER_VOLUME) {
    struct uds_request *request = newReadRequest(page);
    struct cached_page *actual;
    // Make sure the page read is asynchronous
    begin_pending_search(&volume->page_cache, page, 0);
    int result = get_volume_page_protected(volume, request, page, &actual);
    if (result == UDS_SUCCESS) {
      freeReadRequest(request);
      verifyPageData(page, actual, geometry->bytes_per_page);
    } else if (result == UDS_QUEUED) {
      mutex_lock(&numRequestsMutex);
      ++numRequestsQueued;
      mutex_unlock(&numRequestsMutex);
    }
    end_pending_search(&volume->page_cache, 0);
    // back one page 25%, same page 25%, forward one page 50%.
    unsigned int action = random() % 4;
    if (action == 0) {
      if (page > HEADER_PAGES_PER_VOLUME) {
        --page;
      }
    } else if (action != 1) {
      ++page;
    }
  }
  mutex_lock(&numRequestsMutex);
  while (numRequestsQueued > 0) {
    CU_ASSERT_TRUE(waitCondTimeout(&allDoneCond, &numRequestsMutex, seconds_to_ktime(10)));
  }
  mutex_unlock(&numRequestsMutex);
}

/**********************************************************************/
static void testFullReadQueue(void)
{
  init(retryReadRequest, 1);

  const unsigned int numRequests = VOLUME_CACHE_MAX_QUEUED_READS;
  struct uds_request **requests;
  UDS_ASSERT_SUCCESS(vdo_allocate(numRequests, struct uds_request *, __func__, &requests));

  volume->read_threads_stopped = true;
  unsigned int i;
  for (i = 0; i < numRequests; i++) {
    u32 page = HEADER_PAGES_PER_VOLUME + i;
    requests[i] = newReadRequest(page);
    bool queued = enqueue_read(&volume->page_cache, requests[i], page);
    if (i < numRequests - 1) {
      CU_ASSERT_TRUE(queued);

      mutex_lock(&numRequestsMutex);
      ++numRequestsQueued;
      mutex_unlock(&numRequestsMutex);
    } else {
      CU_ASSERT_FALSE(queued);
    }
  }

  volume->read_threads_stopped = false;
  mutex_lock(&volume->read_threads_mutex);
  enqueue_page_read(volume, requests[numRequests - 1], numRequests - 1);
  mutex_unlock(&volume->read_threads_mutex);
  mutex_lock(&numRequestsMutex);
  ++numRequestsQueued;
  mutex_unlock(&numRequestsMutex);

  mutex_lock(&numRequestsMutex);
  while (numRequestsQueued > 0) {
    CU_ASSERT_TRUE(waitCondTimeout(&allDoneCond, &numRequestsMutex, seconds_to_ktime(60)));
  }
  mutex_unlock(&numRequestsMutex);

  vdo_free(requests);
}

/**********************************************************************/
static void testInvalidateReadQueue(void)
{
  init(retryReadRequest, 1);

  const unsigned int numRequests = VOLUME_CACHE_MAX_QUEUED_READS;
  struct uds_request **requests;
  UDS_ASSERT_SUCCESS(vdo_allocate(numRequests, struct uds_request *, __func__, &requests));

  // Fill up the read queue by stopping the read threads and enqueuing entries
  volume->read_threads_stopped = true;
  unsigned int i;
  for (i = 0; i < numRequests; i++) {
    u32 page = HEADER_PAGES_PER_VOLUME + i;
    requests[i] = newReadRequest(page);
    bool queued = enqueue_read(&volume->page_cache, requests[i], page);
    if (i < numRequests - 1) {
      CU_ASSERT_TRUE(queued);

      mutex_lock(&numRequestsMutex);
      ++numRequestsQueued;
      mutex_unlock(&numRequestsMutex);
    } else {
      CU_ASSERT_FALSE(queued);
    }
  }

  // Invalidate all of the reads, so that when they're dequeued, they don't
  // push the synchronized read out of the cache
  mutex_lock(&volume->read_threads_mutex);
  for (i = 0; i < geometry->pages_per_volume; i++) {
    invalidate_page(&volume->page_cache, i + HEADER_PAGES_PER_VOLUME);
  }

  // Synchronously read in physical page 5. We skip entry 0, as that is the
  // configuration page for the volume.
  struct cached_page *actual;
  UDS_ASSERT_SUCCESS(get_volume_page_locked(volume, 5, &actual));
  CU_ASSERT_PTR_NOT_NULL(actual);
  mutex_unlock(&volume->read_threads_mutex);

  volume->read_threads_stopped = false;
  mutex_lock(&volume->read_threads_mutex);
  // This enqueue will wake the reader threads to process the now invalid reads
  enqueue_page_read(volume, requests[numRequests - 1], numRequests - 1);
  mutex_unlock(&volume->read_threads_mutex);
  mutex_lock(&numRequestsMutex);
  ++numRequestsQueued;
  mutex_unlock(&numRequestsMutex);

  mutex_lock(&numRequestsMutex);
  while (numRequestsQueued > 0) {
    CU_ASSERT_TRUE(waitCondTimeout(&allDoneCond, &numRequestsMutex, seconds_to_ktime(60)));
  }
  mutex_unlock(&numRequestsMutex);

  // Try to get page 5 from the map. It should be there from the sync read
  mutex_lock(&volume->read_threads_mutex);
  get_page_from_cache(&volume->page_cache, 5, &actual);
  CU_ASSERT_PTR_NOT_NULL(actual);
  mutex_unlock(&volume->read_threads_mutex);

  vdo_free(requests);
}

/**********************************************************************/
static unsigned int randomChapter(void)
{
  return random() % geometry->chapters_per_volume;
}

/**********************************************************************/
static unsigned int randomPage(void)
{
  return random() % geometry->pages_per_chapter;
}

/**********************************************************************/
static void retryReadRequestAndVerifyMT(struct uds_request *request)
{
  ReadRequest *readRequest = container_of(request, ReadRequest, request);
  uint32_t physicalPage = readRequest->physicalPage;

  struct cached_page *actual;
  // Make sure the page read is synchronous. We do not need to grab
  // the volume read lock here, because the caller of this function already
  // has it.
  UDS_ASSERT_SUCCESS(get_volume_page_locked(volume, physicalPage, &actual));
  verifyPageData(physicalPage, actual, geometry->bytes_per_page);

  if (request->requeued) {
    keepRunning = false;
  }

  retryReadRequest(request);
}

static const unsigned int MAX_REQUESTS = 102400;
static unsigned int iterationCounter = 0;

typedef struct {
  unsigned int zoneNumber;
} ThreadArg;

/**********************************************************************/
static void invalidatePageThread(void *arg __attribute__((unused)))
{
  while (keepRunning) {
    mutex_lock(&volume->read_threads_mutex);
    u32 physicalPage = map_to_physical_page(geometry, randomChapter(), randomPage());
    invalidate_page(&volume->page_cache, physicalPage);
    mutex_unlock(&volume->read_threads_mutex);
    cond_resched();

  }
}

/**********************************************************************/
static void indexThreadAsync(void *arg)
{
  ThreadArg *a = (ThreadArg *) arg;
  unsigned int zoneNumber = a->zoneNumber;

  while (iterationCounter < MAX_REQUESTS) {
    uds_signal_cond(&volume->read_threads_cond);

    u32 physicalPage = map_to_physical_page(geometry, randomChapter(), randomPage());

    // Only one of the async threads needs to keep track of the number of
    // iterations it has run.
    if (zoneNumber == 0) {
      iterationCounter++;
    }

    struct cached_page *entry = NULL;
    struct uds_request *request = newReadRequest(physicalPage);
    request->zone_number = zoneNumber;

    begin_pending_search(&volume->page_cache, physicalPage, zoneNumber);

    // Assume we're enqueuing this
    mutex_lock(&numRequestsMutex);
    ++numRequestsQueued;
    mutex_unlock(&numRequestsMutex);

    int result = get_volume_page_protected(volume, request, physicalPage, &entry);
    if (result == UDS_SUCCESS) {
      freeReadRequest(request);
      verifyPageData(physicalPage, entry, geometry->bytes_per_page);

      // We didn't actually enqueue this particular request, so adjust the count
      // we're waiting on
      mutex_lock(&numRequestsMutex);
      --numRequestsQueued;
      mutex_unlock(&numRequestsMutex);
    } else {
      CU_ASSERT_EQUAL(result, UDS_QUEUED);
    }

    end_pending_search(&volume->page_cache, zoneNumber);
    cond_resched();
  }

  uds_signal_cond(&volume->read_threads_cond);

  keepRunning = false;
}

/**********************************************************************/
static void testMultiThreadStress(unsigned int numAsyncIndexThreads)
{
  /*
   * Use three types of threads to try and mess things up as much as possible.
   * - Index threads doing async searches
   * - Regular reader threads reading in entries from disk
   * - A thread which is periodically invalidating chapters
   */

  const unsigned int numZones = numAsyncIndexThreads;
  const unsigned int numThreads = 1 + numZones;

  init(retryReadRequestAndVerifyMT, numZones);
  const unsigned int numRequests = VOLUME_CACHE_MAX_QUEUED_READS;
  keepRunning = true;

  // Fill up the read queue by stopping the read threads and enqueuing entries
  volume->read_threads_stopped = true;
  unsigned int i;
  for (i = 0; i < numRequests; i++) {
    u32 page = HEADER_PAGES_PER_VOLUME + i;
    struct uds_request *request = newReadRequest(page);
    bool queued = enqueue_read(&volume->page_cache, request, page);
    if (i < numRequests - 1) {
      CU_ASSERT_TRUE(queued);

      mutex_lock(&numRequestsMutex);
      ++numRequestsQueued;
      mutex_unlock(&numRequestsMutex);
    } else {
      CU_ASSERT_FALSE(queued);
      freeReadRequest(request);
    }
  }
  volume->read_threads_stopped = false;

  ThreadArg *args;
  UDS_ASSERT_SUCCESS(vdo_allocate(numZones, ThreadArg, __func__, &args));
  unsigned int k;
  for (k = 0; k < numZones; k++) {
    args[k].zoneNumber = k;
  }

  struct thread **threads;
  UDS_ASSERT_SUCCESS(vdo_allocate(numThreads, struct thread *, __func__, &threads));

  int result = UDS_SUCCESS;
  for (i = 0; i < numAsyncIndexThreads; i++) {
    char nameBuf[100];
    UDS_ASSERT_SUCCESS(vdo_fixed_sprintf(nameBuf, sizeof(nameBuf), "asyncIndex%d", i));
    result = vdo_create_thread(indexThreadAsync, &args[i], nameBuf, &threads[i]);
    UDS_ASSERT_SUCCESS(result);
  }

  result = vdo_create_thread(invalidatePageThread, NULL, "invalidPage", &threads[i++]);
  UDS_ASSERT_SUCCESS(result);

  CU_ASSERT_EQUAL(i, numThreads);

  for (i = 0; i < numThreads; ++i) {
    vdo_join_threads(threads[i]);
  }

  mutex_lock(&numRequestsMutex);
  while (numRequestsQueued > 0) {
    CU_ASSERT_TRUE(waitCondTimeout(&allDoneCond, &numRequestsMutex, seconds_to_ktime(60)));
  }
  mutex_unlock(&numRequestsMutex);

  vdo_free(threads);
  vdo_free(args);
}

/**********************************************************************/
static void testMultiThreadStress1Async(void)
{
  testMultiThreadStress(1);
}

/**********************************************************************/
static void testMultiThreadStress4Async(void)
{
  testMultiThreadStress(4);
}

/**********************************************************************/
static const CU_TestInfo tests[] = {
  {"Invalid Read Queue", testInvalidateReadQueue},
  {"SequentialGet",      testSequentialGet},
  {"Full Read Queue",    testFullReadQueue},
  {"MT Stress 1 async",  testMultiThreadStress1Async},
  {"MT Stress 4 async",  testMultiThreadStress4Async},
  CU_TEST_INFO_NULL,
  {"StumblingGet",       testStumblingGet},
  CU_TEST_INFO_NULL,
};

static const CU_SuiteInfo suite = {
  .name    = "Volume_n4",
  .cleaner = deinit,
  .tests   = tests,
};

const CU_SuiteInfo *initializeModule(void)
{
  return &suite;
}
