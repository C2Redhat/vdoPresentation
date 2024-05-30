#! /usr/bin/python3
"""
  alltests.py - Generator tool for combination unit tests

  %COPYRIGHT%

  %LICENSE%

  $Id$

"""

import sys

suite   = sys.argv[1]
singles = ['initializeModule_' + single for single in sys.argv[2].split()]

def writeSingleSuiteInfo():
  for single in singles:
    print('extern const CU_SuiteInfo *' + single + '(void);')

  print('\nstatic const CU_SuiteInfo *(* const singleFns[])(void) = {')
  for single in singles:
    print('  ' + single + ',')
  print('  NULL,')
  print('};')

# Write header info
print('''/*
 * This file is generated by alltests.py
 */

#include "albtest.h"
#include "albtestCommon.h"
''')

writeSingleSuiteInfo()

# Write combined initializeModule method
print('''
static const CU_SuiteInfo *suites = NULL;

/**********************************************************************/
static void cleanerTest(void)
{
  /*
   * This is freeing memory that was allocated by initializeModule.  We depend
   * upon this test being called at least once.
   */
  freeSuites(suites);
  suites = NULL;
}

/**********************************************************************/

static const CU_TestInfo tests[] = {
  { "alltests cleaner", cleanerTest },
  CU_TEST_INFO_NULL,
};

const CU_SuiteInfo *initializeModule(void)
{
  if (suites == NULL) {
    const CU_SuiteInfo **pSuites = &suites;
    const CU_SuiteInfo alltest_suite = {
      .name    = "''' + suite +  '''",
      .tests   = tests,
      .mustRun = true,
    };
    appendSuites(&pSuites, &alltest_suite);
    unsigned int i;
    for (i = 0; singleFns[i] != NULL; i++) {
      const CU_SuiteInfo *suite = (*singleFns[i])();
      appendSuites(&pSuites, suite);
    }
  }
  return suites;
}
''')

