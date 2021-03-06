// Cuckoo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2017 John Tromp

#include <stdint.h> // for types uint32_t,uint64_t
#include <string.h> // for functions strlen, memset
#include <stdio.h>
//#include <openssl/sha.h>  //Removing for now, to see if we can live without it
                            //Would give a dependency on openssl we'd rather avoid
#include "siphash.h"
#include "hash_impl.h"

#define GRIN_MOD 1
#define SQUASH_OUTPUT 0

#if !GRIN_MOD
// proof-of-work parameters
#ifndef EDGEBITS
// the main parameter is the 2-log of the graph size,
// which is the size in bits of the node identifiers
#define EDGEBITS 27
#endif
#else
//Just transform above into a global for now, to minimise changes needed from
//original
int EDGEBITS=27;
#endif

#ifndef PROOFSIZE
// the next most important parameter is the (even) length
// of the cycle to be found. a minimum of 12 is recommended
#define PROOFSIZE 42
#endif

//Need to figure out how to deal with these typedefs
#if EDGEBITS > 32
typedef u64 edge_t;
#else
typedef u32 edge_t;
#endif
#if EDGEBITS > 31
typedef u64 node_t;
#else
typedef u32 node_t;
#endif

// number of edges
#define NEDGES ((node_t)1 << EDGEBITS)
// used to mask siphash output
#define EDGEMASK ((edge_t)NEDGES - 1)

// generate edge endpoint in cuckoo graph without partition bit
edge_t _sipnode(siphash_keys *keys, edge_t nonce, u32 uorv) {
  return siphash24(keys, 2*nonce + uorv) & EDGEMASK;
}

// generate edge endpoint in cuckoo graph
node_t sipnode(siphash_keys *keys, edge_t nonce, u32 uorv) {
  return _sipnode(keys, nonce, uorv) << 1 | uorv;
}

enum verify_code { POW_OK, POW_HEADER_LENGTH, POW_TOO_BIG, POW_TOO_SMALL, POW_NON_MATCHING, POW_BRANCH, POW_DEAD_END, POW_SHORT_CYCLE};
const char *errstr[] = { "OK", "wrong header length", "nonce too big", "nonces not ascending", "endpoints don't match up", "branch in cycle", "cycle dead ends", "cycle too short"};

// length of header hashed into siphash key
#ifndef HEADERLEN
#define HEADERLEN 80
#endif

//Just keep this around for debugging
static void print_buf(const char *title, const unsigned char *buf, size_t buf_len)
{
    size_t i = 0;
    printf("%s\n", title);
    for(i = 0; i < buf_len; ++i)
    printf("%02X%s", buf[i],
             ( i + 1 ) % 16 == 0 ? "\r\n" : " " );

}

void SHA256(unsigned char * in, u32 len, unsigned char* out){
    //print_buf("Input for SHA256 for key: ", in, len);
    secp256k1_sha256_t sha;
    secp256k1_sha256_initialize(&sha);
    secp256k1_sha256_write(&sha, in, len);
    secp256k1_sha256_finalize(&sha, out);
    //print_buf("SHA256 for key: ", out, len);
    


}

void setheader(const char *header, const u32 headerlen, siphash_keys *keys) {
  char hdrkey[32];
  SHA256((unsigned char *)header, headerlen, (unsigned char *)hdrkey);
  setkeys(keys, hdrkey);
}

// verify that nonces are ascending and form a cycle in header-generated graph
int verify(edge_t nonces[PROOFSIZE], const char *header, const u32 headerlen) {
  if (headerlen != HEADERLEN)
    return POW_HEADER_LENGTH;
  siphash_keys keys;
  setheader(header, headerlen, &keys);
  node_t uvs[2*PROOFSIZE];
  node_t xor0=0,xor1=0;
  for (u32 n = 0; n < PROOFSIZE; n++) {
    if (nonces[n] > EDGEMASK)
      return POW_TOO_BIG;
    if (n && nonces[n] <= nonces[n-1])
      return POW_TOO_SMALL;
    xor0 ^= uvs[2*n  ] = sipnode(&keys, nonces[n], 0);
    xor1 ^= uvs[2*n+1] = sipnode(&keys, nonces[n], 1);
  }
  if (xor0|xor1)              // matching endpoints imply zero xors
    return POW_NON_MATCHING;
  u32 n = 0, i = 0, j;
  do {                        // follow cycle
    for (u32 k = j = i; (k = (k+2) % (2*PROOFSIZE)) != i; ) {
      if (uvs[k] == uvs[i]) { // find other edge endpoint identical to one at i
        if (j != i)           // already found one before
          return POW_BRANCH;
        j = k;
      }
    }
    if (j == i) return POW_DEAD_END;  // no matching endpoint
    i = j^1;
    n++;
  } while (i != 0);           // must cycle back to start or we would have found branch
  return n == PROOFSIZE ? POW_OK : POW_SHORT_CYCLE;
}
