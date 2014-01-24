

#include "md5_64bit.h"



// 返回一个md5值的前64 bit
uint64 first_half_md5(const char * str, unsigned int len)
{
	unsigned char result[16];
	memset(result, 0, 16);

	MD5((unsigned char *)str, len, result);

	uint64 tmp_code = 0;

	tmp_code += (uint64)result[0]<<56;  //printf("%02x", result[0]);
	tmp_code += (uint64)result[1]<<48; //printf("%02x", result[1]);
	tmp_code += (uint64)result[2]<<40;  //printf("%02x", result[2]);
	tmp_code += (uint64)result[3]<<32;  //printf("%02x", result[3]);
	tmp_code += (uint64)result[4]<<24;  //printf("%02x", result[4]);
	tmp_code += (uint64)result[5]<<16;  //printf("%02x", result[5]);
	tmp_code += (uint64)result[6]<<8;  //printf("%02x", result[6]);
	tmp_code += (uint64)result[7];   //printf("%02x", result[7]);

	return tmp_code;	
}

uint64 string_md5(const char * str, unsigned int len)
{
    long y = 0;

    for(unsigned int i=0; i<12 && i<len;i++)
    {
        y += str[i] - 'a';
        y *= 26;
    }

    if (len > 12)
        y += str[len-1] - 'a';

    return y;
}


/*
int main(void)
{
	unsigned char  md[33]  ;
	md[0] = 'q';
	md[1] = 'i';
	md[2] = 'a';
	md[3] = 'o';
	md[4] = 'y';
	md[5] = 'o';
	md[6] = 'n';
	md[7] = 'g';
	md[8] = '\0';
        unsigned char result[33];
        memset(result, 0, 33);

	MD5(md, 8, result);

        for(int i =0; i<16; i++)
        {
        printf("%02x", result[i]);
        }
        printf("\n");

}
*/
