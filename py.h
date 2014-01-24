#ifndef __CN2PY__
#define __CN2PY__



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 这个使用的地方比较多 单独存好了
#include <google/dense_hash_map>
#include <openssl/md5.h>

#include <sparsehash/dense_hash_map>

#include "common_def.h"
#include "md5_64bit.h"



using google::dense_hash_map;      // namespace where class lives by default
//using std::cout;
//using std::endl;

/*
    如下是测试代码

    typedef unsigned long long uint64;

    unsigned long long my_hash(const char * str)
    {
            unsigned char result[16];
            memset(result, 0, 16);

            unsigned long len = (unsigned long)( strlen((char *)str) ) ; 

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

    struct OwnHash
    {
        size_t operator()(uint64 key) const
        {   
            return key;
        }   
    };


    int main(void)
    {
            dense_hash_map<uint64, int, OwnHash > cn2py;

            cn2py.set_empty_key(0);
            cn2py[my_hash("qiaoyong")] = 1;
            cn2py[my_hash("jingping")] = 2;
            cn2py[my_hash("sina")] = 3;
            cn2py[my_hash("sohu")] = 4;

            printf("%d\n", cn2py[my_hash("sohu")]);



            return 0;
    }

*/

/*

enum
{
	MAX_CNPY_SEGMENT_NUMBER = 64,
	MAX_CNPY_NUMBER = 128,
    MAX_RESULT_BUFFER = 256,
};

*/


// 存储汉字query到拼音的结果. 由于多音字的存在, 所以多个结果.
// "刘的华"的结果
//     "liudehua"  "liu" "de" "hua"
//     "liudihua"  "liu" "di" "hua"
//
// "h&m 官网"
// 		"h&m guanwang"  "h&m "  "guan" "wang"
// 
// 概率表有. 暂时不考虑使用. 非多音字,直接乘1, 多音字对应的概率乘起来即可. 
class cn2py_segment
{
    public:
        vector<string> py_list;        


    	cn2py_segment(); //为了系统初始化词典设计的
        cn2py_segment(const char * query); //应该使用的这个接口
        ~cn2py_segment();

        void debug(void); // 打印元素系统信息


    private:
    	static THash table_cn2py;
        static int init_flag;

        char file_cn2py[32];

        char query[128];
        vector<string> word_list;

        void load_dict(void); 
        int fullfil_hash(const char * file_name, dense_hash_map<uint64, char *, OwnHash> &table);
        int split_query_segment(const char * p);    
        
};



#endif
