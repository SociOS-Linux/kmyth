/*
 * Kmyth Unsealing Interface - TPM 2.0
 */

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <sys/stat.h>

#include "defines.h"
#include "file_io.h"
#include "kmyth.h"
#include "kmyth_log.h"
#include "memory_util.h"

static void usage(const char *prog)
{
  fprintf(stdout,
          "\nusage: %s [options]\n\n"
          "options are: \n\n"
          " -a or --auth_string   String used to create 'authVal' digest. Defaults to empty string (all-zero digest).\n"
          " -i or --input         Path to file containing data the to be unsealed\n"
          " -o or --output        Destination path for unsealed file. This or -s must be specified. Will not overwrite any\n"
          "                       existing files unless the 'force' option is selected.\n"
          " -f or --force         Force the overwrite of an existing output file\n"
          " -s or --stdout        Output unencrypted result to stdout instead of file.\n"
          " -p or --policy_or     Unseals a file sealed using a compound \"policy or\".\n"
          " -w or --owner_auth    TPM 2.0 storage (owner) hierarchy authorization. Defaults to emptyAuth to match TPM default.\n"
          " -v or --verbose       Enable detailed logging.\n"
          " -h or --help          Help (displays this usage).\n", prog);
}

const struct option longopts[] = {
  {"auth_string", required_argument, 0, 'a'},
  {"input", required_argument, 0, 'i'},
  {"output", required_argument, 0, 'o'},
  {"force", no_argument, 0, 'f'},
  {"policy_or", no_argument, 0, 'p'},
  {"owner_auth", required_argument, 0, 'w'},
  {"standard", no_argument, 0, 's'},
  {"verbose", no_argument, 0, 'v'},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
  // If no command line arguments provided, provide usage help and exit early
  if (argc == 1)
  {
    usage(argv[0]);
    return 0;
  }

  // Configure logging messages
  set_app_name(KMYTH_APP_NAME);
  set_app_version(KMYTH_VERSION);
  set_applog_path(KMYTH_APPLOG_PATH);

  // Initialize parameters that might be modified by command line options
  char *inPath = NULL;
  char *outPath = NULL;
  bool stdout_flag = false;
  char *authString = NULL;
  char *ownerAuthPasswd = "";
  bool forceOverwrite = false;
  uint8_t bool_policy_or = 0;
  int options;
  int option_index;

  // Parse and apply command line options
  while ((options = getopt_long(argc, argv, "a:i:o:w:fhpsv", longopts,
                                &option_index)) != -1)
  {
    switch (options)
    {
    case 'a':
      authString = optarg;
      break;
    case 'f':
      forceOverwrite = true;
      break;
    case 'p':
      bool_policy_or = 1;
      break;
    case 'i':
      inPath = optarg;
      break;
    case 'o':
      outPath = optarg;
      break;
    case 'w':
      ownerAuthPasswd = optarg;
      break;
    case 'v':
      // always display all log messages (severity threshold = LOG_DEBUG)
      // to stdout or stderr (output mode = 0)
      set_applog_severity_threshold(LOG_DEBUG);
      set_applog_output_mode(0);
      break;
    case 's':
      stdout_flag = true;
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      return 1;
    }
  }

  //Since these originate in main() we know they are null terminated
  size_t auth_string_len = (authString == NULL) ? 0 : strlen(authString);
  size_t oa_passwd_len =
    (ownerAuthPasswd == NULL) ? 0 : strlen(ownerAuthPasswd);

  // Check that input path (file to be sealed) was specified
  if (inPath == NULL || (outPath == NULL && stdout_flag == false))
  {
    kmyth_log(LOG_ERR,
              "Input file and output file (or stdout) must both be specified ... exiting");
    kmyth_clear(authString, auth_string_len);
    kmyth_clear(ownerAuthPasswd, oa_passwd_len);
    return 1;
  }
  else
  {
    if (verifyInputFilePath(inPath))
    {
      kmyth_log(LOG_ERR, "invalid input path (%s) ... exiting", inPath);
      kmyth_clear(authString, auth_string_len);
      kmyth_clear(ownerAuthPasswd, oa_passwd_len);
      return 1;
    }
  }
  // If output to be written to file - validate that path
  if (stdout_flag == false)
  {
    // Verify output path
    if (verifyOutputFilePath(outPath))
    {
      kmyth_log(LOG_ERR, "kmyth-unseal encountered invalid outfile path");
      kmyth_clear(authString, auth_string_len);
      kmyth_clear(ownerAuthPasswd, oa_passwd_len);
      return 1;
    }

    // If 'force overwrite' flag not set, make sure filename doesn't exist
    if (!forceOverwrite)
    {
      struct stat st = { 0 };
      if (!stat(outPath, &st))
      {
        kmyth_log(LOG_ERR,
                  "output filename (%s) already exists ... exiting", outPath);
        kmyth_clear(authString, auth_string_len);
        kmyth_clear(ownerAuthPasswd, oa_passwd_len);
        return 1;
      }
    }
  }

  // Call top-level "kmyth-unseal" function
  uint8_t *output = NULL;
  size_t output_length = 0;

  if (tpm2_kmyth_unseal_file(inPath, &output, &output_length,
                             (uint8_t *) authString, auth_string_len,
                             (uint8_t *) ownerAuthPasswd, oa_passwd_len,
                             bool_policy_or))
  {
    kmyth_clear_and_free(output, output_length);
    kmyth_log(LOG_ERR, "kmyth-unseal failed ... exiting");
    kmyth_clear(authString, auth_string_len);
    kmyth_clear(ownerAuthPasswd, oa_passwd_len);
    return 1;
  }
  // We are done with authString and ownerAuthPasswd, so clear them
  kmyth_clear(authString, auth_string_len);
  kmyth_clear(ownerAuthPasswd, oa_passwd_len);

  if (stdout_flag == true)
  {
    if (print_to_stdout(output, output_length))
    {
      kmyth_log(LOG_ERR, "error printing to stdout");
    }
  }
  else
  {
    if (write_bytes_to_file(outPath, output, output_length))
    {
      kmyth_log(LOG_ERR, "Error writing file: %s", outPath);
    }
    else
    {
      kmyth_log(LOG_DEBUG, "unsealed contents of %s to %s", inPath, outPath);
    }
  }

  kmyth_clear_and_free(output, output_length);

  return 0;
}
