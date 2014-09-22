/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "scrypt_platform.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "readpass.h"
#include "genpass.h"
#include "warn.h"

static void
usage(void)
{

  fprintf(stderr,
      "Usage: scrypt-genpass [-l LEN] [-m MAXMEM] [-n] [-o MAXOPS] [-k KEYFILE]\n");
  fprintf(stderr,
      "                      [-p PASS] [-r] [-v] <site>\n");
  fprintf(stderr,
      "       scrypt-genpass -t\n");
  fprintf(stderr,
      "\nFor documentation, see https://github.com/chrisoei/scrypt-genpass/wiki\n\n");
  fprintf(stderr, "Commit hash: %s\n", SGVERSION);
  exit(1);
}

void unit_tests()
{
  if (sizeof(char)!=1) {
    fprintf(stderr, "sizeof(char) != 1\n");
    exit(1);
  }

  uint8_t testhash[32];
  sha256string(testhash, (uint8_t*) "abc", 3);
  char testbuf[65];
  bintohex(testbuf, 32, testhash);
  if (strcmp(testbuf,
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
    fprintf(stderr, "SHA256 test failed\n");
    exit(1);
  }

  fprintf(stderr, "All internal tests pass\n");
  exit(0);
}

void init_parms(sg_parms_ptr p)
{
  p->passwdlen = 0;
  p->outputlength = 16;
  p->maxmem = 1000;
  p->megaops = 32;
  p->keyfile = NULL;
  p->passwd = NULL;
  p->numbers_only = 0;
  p->verbose = 0;
}

int
main(int argc, char *argv[])
{
  sg_parms_t sg_parms;

  char ch;
  int rc;
  bool repeat = false;

#ifdef NEED_WARN_PROGNAME
  warn_progname = "scrypt-genpass";
#endif

  if (argc < 1)
    usage();

  init_parms(&sg_parms);
  /* Parse arguments. */
  while ((ch = getopt(argc, argv, "htk:l:m:no:p:rv")) != -1) {
    switch (ch) {
    case 'k':
      sg_parms.keyfile = strdup(optarg);
      break;
    case 'l':
      sg_parms.outputlength = atoi(optarg);
      break;
    case 'm':
      sg_parms.maxmem = atoi(optarg);
      break;
    case 'n':
      sg_parms.numbers_only++;
      break;
    case 'o':
      sg_parms.megaops = atoi(optarg);
      break;
    case 'p':
      sg_parms.passwd = strdup(optarg);
      break;
    case 't':
      unit_tests();
      break;
    case 'r':
      repeat = true;
      break;
    case 'v':
      sg_parms.verbose = 1;
      break;
    default:
      usage();
    }
  }
  argc -= optind;
  argv += optind;

  /* We must have one parameters left. */
  if (argc != 1)
    usage();

  if (!sg_parms.passwd) {
    /* Prompt for a password. */
    if (tarsnap_readpass((char**)&(sg_parms.passwd), "Please enter passphrase",
        (repeat ? "Please repeat passphrase" : NULL), 1))
      exit(1);
  }
  sg_parms.passwdlen = strlen(sg_parms.passwd);

  if (sg_parms.keyfile) {
    FILE *fp;
    size_t keyfilelen;

    fp = fopen(sg_parms.keyfile, "rb");
    if (fp) {
      fseek(fp, 0, SEEK_END);
      keyfilelen = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      uint8_t* combinedkey = malloc(sg_parms.passwdlen + keyfilelen + 1);
      if (combinedkey) {
        strcpy(combinedkey, sg_parms.passwd);
        memset(sg_parms.passwd, 0, sg_parms.passwdlen);
        free(sg_parms.passwd);
        size_t n  = fread(combinedkey + sg_parms.passwdlen, keyfilelen, 1, fp);
        fclose(fp);
        if (n != 1) {
          warnx("Unable to read keyfile");
          exit(1);
        }
        sg_parms.passwd = combinedkey;
        sg_parms.passwdlen += keyfilelen;
      } else {
        warnx("Unable to allocate memory for combined key");
        exit(1);
      }
    } else {
      warn("Unable to open keyfile %s", sg_parms.keyfile);
      exit(1);
    }
  }

  uint8_t passhash[32];
  sha256string(passhash, sg_parms.passwd, sg_parms.passwdlen);
  if (sg_parms.verbose) {
    char buf1[65];
    bintohex(buf1, 32, passhash);
    printf("Master hex: %s\n", buf1);
    memset(buf1, 0, 65);
  }

  uint8_t dk[64];
  sg_parms.site = *argv;
  rc = genpass(dk, &sg_parms);

  /* Zero and free the password. */
  memset(sg_parms.passwd, 0, sg_parms.passwdlen);
  free(sg_parms.passwd);
  free(sg_parms.keyfile);

  if (sg_parms.verbose) {
    char buf[129];
    bintohex(buf, 64, dk);
    printf("Pass hex: %s\n", buf);
    memset(buf, 0, 129);
  }

  if ((sg_parms.outputlength < 3)||(sg_parms.outputlength > 64)) {
    warn("Unable to generate password for output length %lu", sg_parms.outputlength);
    exit(1);
  }

  char output[sg_parms.outputlength + 1];
  hashtopass(sg_parms.numbers_only, output, sg_parms.outputlength, dk);
  printf((sg_parms.verbose ? "Generated password: %s\n" : "%s\n"), output);
  memset(output, 0, sg_parms.outputlength + 1);

  /* If we failed, print the right error message and exit. */
  if (rc != 0) {
    switch (rc) {
    case 1:
      warn("Error determining amount of available memory");
      break;
    case 2:
      warn("Error reading clocks");
      break;
    case 3:
      warn("Error computing derived key");
      break;
    case 4:
      warn("Error reading salt");
      break;
    case 5:
      warn("OpenSSL error");
      break;
    case 6:
      warn("Error allocating memory");
      break;
    case 7:
      warnx("Input is not valid scrypt-encrypted block");
      break;
    case 8:
      warnx("Unrecognized scrypt format version");
      break;
    case 9:
      warnx("Decrypting file would require too much memory");
      break;
    case 10:
      warnx("Decrypting file would take too much CPU time");
      break;
    case 11:
      warnx("Passphrase is incorrect");
      break;
    case 12:
      warn("Error writing file: %s",
          (argc > 1) ? argv[1] : "standard output");
      break;
    case 13:
      warn("Error reading file: %s", argv[0]);
      break;
    case 14:
      warn("Unable to open keyfile: %s", sg_parms.keyfile);
      break;
    case 15:
      warn("Unable to allocate memory for combined key");
      break;
    }
    exit(1);
  }

  return (0);
}
