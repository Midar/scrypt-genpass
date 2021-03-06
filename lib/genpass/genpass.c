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
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */
#include "scrypt_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/aes.h>

#include "crypto_scrypt.h"
#include "sha256.h"
#include "sysendian.h"

#include "genpass.h"

#define ENCBLOCK 65536
#define MEGA (1024*1024)

static int pickparams(uint32_t, uint32_t,
    int *, uint32_t *, uint32_t *);
static int checkparams(uint32_t, uint32_t, int, uint32_t, uint32_t);
static int getsalt(uint8_t[32], char* site, bool verbose);

static int
pickparams(uint32_t maxmem, uint32_t megaops,
    int * logN, uint32_t * r, uint32_t * p)
{
  size_t memlimit;
  double opps;
  double opslimit;
  double maxN, maxrp;
  int rc;

  /* Figure out how much memory to use. */
  memlimit = MEGA * maxmem;
  opslimit = MEGA * megaops;

  /* Fix r = 8 for now. */
  *r = 8;

  /*
   * The memory limit requires that 128Nr <= memlimit, while the CPU
   * limit requires that 4Nrp <= opslimit.  If opslimit < memlimit/32,
   * opslimit imposes the stronger limit on N.
   */
#ifdef DEBUG
  fprintf(stderr, "Requiring 128Nr <= %zu, 4Nrp <= %f\n",
      memlimit, opslimit);
#endif
  if (opslimit < memlimit/32) {
    /* Set p = 1 and choose N based on the CPU limit. */
    *p = 1;
    maxN = opslimit / (*r * 4);
    for (*logN = 1; *logN < 63; *logN += 1) {
      if ((uint64_t)(1) << *logN > maxN / 2)
        break;
    }
  } else {
    /* Set N based on the memory limit. */
    maxN = memlimit / (*r * 128);
    for (*logN = 1; *logN < 63; *logN += 1) {
      if ((uint64_t)(1) << *logN > maxN / 2)
        break;
    }

    /* Choose p based on the CPU limit. */
    maxrp = (opslimit / 4) / ((uint64_t)(1) << *logN);
    if (maxrp > 0x3fffffff)
      maxrp = 0x3fffffff;
    *p = (uint32_t)(maxrp) / *r;
  }

#ifdef DEBUG
  fprintf(stderr, "N = %zu r = %d p = %d\n",
      (size_t)(1) << *logN, (int)(*r), (int)(*p));
#endif

  /* Success! */
  return (0);
}

static int
checkparams(uint32_t maxmem, uint32_t megaops,
    int logN, uint32_t r, uint32_t p)
{
  size_t memlimit;
  double opps;
  double opslimit;
  uint64_t N;
  int rc;

  /* Figure out the maximum amount of memory we can use. */
  memlimit = 1000000 * maxmem;

  opslimit = 1000000 * megaops;

  /* Sanity-check values. */
  if ((logN < 1) || (logN > 63))
    return (7);
  if ((uint64_t)(r) * (uint64_t)(p) >= 0x40000000)
    return (7);

  /* Check limits. */
  N = (uint64_t)(1) << logN;
  if ((memlimit / N) / r < 128)
    return (9);
  if ((opslimit / N) / (r * p) < 4)
    return (10);

  /* Success! */
  return (0);
}

int
bintohex(char* outstring, size_t nbytes, uint8_t* data)
{
  int i;
  for (i = 0; i < nbytes; i++)
    sprintf(outstring + 2 * i, "%02x", data[i]);
  outstring[2 * nbytes] = '\0';
  return 0;
}

void
sha256string(uint8_t hash[32], uint8_t* s, int n)
{
  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);
  SHA256_Update(&sha256_ctx, (void*) s, n);
  SHA256_Final(hash, &sha256_ctx);
}

static int
getsalt(uint8_t salt[32], char* site, bool verbose)
{
  sha256string(salt, (uint8_t*) site, strlen(site));
  if (verbose) {
    char buf[65];
    bintohex(buf, 32, salt);
    printf("Site hex: %s\n", buf);
  }
  return (0);
}

int
genpass(uint8_t dk[64], sg_parms_t *sg_parms)
{
  uint8_t salt[32];
  uint8_t hbuf[32];
  int logN;
  uint64_t N;
  uint32_t r;
  uint32_t p;
  SHA256_CTX ctx;
  uint8_t * key_hmac = &dk[32];
  HMAC_SHA256_CTX hctx;
  int rc;

  /* Pick values for N, r, p. */
  if ((rc = pickparams(sg_parms->maxmem, sg_parms->megaops,
      &logN, &r, &p)) != 0)
    return (rc);
  N = (uint64_t)(1) << logN;

  /* Get some salt using the site. */
  if ((rc = getsalt(salt, sg_parms->site, sg_parms->verbose)) != 0)
    return (rc);

  /* Generate the derived keys. */
  if (crypto_scrypt(sg_parms->passwd, sg_parms->passwdlen, salt, 32, N, r, p,
   dk, 64))
    return (3);

  /* Success! */
  return (0);
}
