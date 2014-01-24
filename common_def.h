
#ifndef __COMMON_DEF__
#define __COMMON_DEF__



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 这个使用的地方比较多 单独存好了
#include <google/dense_hash_map>
#include <sparsehash/dense_hash_map>
#include <openssl/md5.h>


using google::dense_hash_map; 
//using std::dense_hash_map; 

#ifdef  QY_DEBUG
#define _LOG(args...)    fprintf(stderr, "%s:%d Inside function %s, log is %s\n", __FILE__, __LINE__, __FUNCTION__, args)
#else
#define _LOG(agrs...)
#endif


typedef unsigned long long uint64;

typedef unsigned int uint32;

typedef unsigned short uint16;

typedef unsigned char uint8;



struct OwnHash
{
    unsigned long operator()(uint64 key) const
    {   
        return key;
    }   
};



//T meads text
typedef dense_hash_map<uint64, char *, OwnHash> THash;
typedef dense_hash_map<uint64, char *, OwnHash>::iterator THashITE;

//M means offset+number storage in 64 bit, offset use 56bit, number use 8bit. 
typedef dense_hash_map<uint64, uint64, OwnHash> MHash;
typedef dense_hash_map<uint64, uint64, OwnHash>::iterator MHashITE;

// U means uint32
// used for offset storage
typedef dense_hash_map<uint64, uint32, OwnHash> UHash;
typedef dense_hash_map<uint64, uint32, OwnHash>::iterator UHashITE;

#endif

