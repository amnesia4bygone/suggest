
#ifndef __MD5_64BIT__
#define __MD5_64BIT__


#include <openssl/md5.h>
#include <stdio.h>
#include <string.h>
#include "common_def.h"


uint64 string_md5(const char * str, unsigned int len);
uint64 first_half_md5(const char * str, unsigned int len);

#endif

