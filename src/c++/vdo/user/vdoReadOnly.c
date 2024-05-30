/*
 * %COPYRIGHT%
 *
 * %LICENSE%
 */

#include <err.h>
#include <getopt.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "errors.h"
#include "fileUtils.h"
#include "logger.h"
#include "string-utils.h"

#include "constants.h"
#include "status-codes.h"

#include "fileLayer.h"
#include "physicalLayer.h"
#include "vdoConfig.h"
#include "vdoVolumeUtils.h"

static const char usageString[] =
  " [--help] filename";

static const char helpString[] =
  "vdoreadonly - Puts a VDO device into read-only mode\n"
  "\n"
  "SYNOPSIS\n"
  "  vdoreadonly filename\n"
  "\n"
  "DESCRIPTION\n"
  "  vdoreadonly forces an existing VDO device into read-only\n"
  "  mode.\n"
  "\n"
  "OPTIONS\n"
  "    --help\n"
  "       Print this help message and exit.\n"
  "\n"
  "    --version\n"
  "       Show the version of vdoreadonly.\n"
  "\n";

// N.B. the option array must be in sync with the option string.
static struct option options[] = {
  { "help",                  no_argument,       NULL, 'h' },
  { "version",               no_argument,       NULL, 'V' },
  { NULL,                    0,                 NULL,  0  },
};
static char optionString[] = "h";

static void usage(const char *progname, const char *usageOptionsString)
{
  errx(1, "Usage: %s%s\n", progname, usageOptionsString);
}

/**********************************************************************/
int main(int argc, char *argv[])
{
  static char errBuf[VDO_MAX_ERROR_MESSAGE_SIZE];

  int result = vdo_register_status_codes();
  if (result != VDO_SUCCESS) {
    errx(1, "Could not register status codes: %s",
         uds_string_error(result, errBuf, VDO_MAX_ERROR_MESSAGE_SIZE));
  }

  int c;
  while ((c = getopt_long(argc, argv, optionString, options, NULL)) != -1) {
    switch (c) {
    case 'h':
      printf("%s", helpString);
      exit(0);
      break;

    case 'V':
      fprintf(stdout, "vdoreadonly version is: %s\n", CURRENT_VERSION);
      exit(0);
      break;

    default:
      usage(argv[0], usageString);
      break;
    };
  }

  if (optind != (argc - 1)) {
    usage(argv[0], usageString);
  }

  char *filename = argv[optind];
  PhysicalLayer *layer;
  result = makeFileLayer(filename, 0, &layer);
  if (result != VDO_SUCCESS) {
    errx(result, "makeFileLayer failed on '%s'", filename);
  }

  result = setVDOReadOnlyMode(layer);
  if (result != VDO_SUCCESS) {
    char buf[VDO_MAX_ERROR_MESSAGE_SIZE];
    errx(result, "setting read-only mode failed on '%s': %s",
         filename, uds_string_error(result, buf, sizeof(buf)));
  }

  // Close and sync the underlying file.
  layer->destroy(&layer);
}
