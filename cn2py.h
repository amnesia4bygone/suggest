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



enum
{
	MAX_CNPY_SEGMENT_NUMBER = 64,
	MAX_CNPY_NUMBER = 128,
    MAX_RESULT_BUFFER = 256,
};




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
    	uint8 query_segment_number;
    	uint8 py_result_number;
        uint8 py_miss_result_number;

    	char py_result_str[MAX_CNPY_NUMBER][MAX_RESULT_BUFFER]; //一个query,最多返回这么多个切拼音的结果
    	char query_segment_str[MAX_CNPY_SEGMENT_NUMBER][MAX_RESULT_BUFFER]; //一个汉字的拼音最多7个字节. 留一个\0
        char py_miss_result_str[MAX_CNPY_NUMBER][MAX_RESULT_BUFFER]; //一个query丢一个segment, 返回他的切拼音的结果

    	float py_str_prob[MAX_CNPY_SEGMENT_NUMBER]; 

    	int do_cn2py();

        // -1, number非法 
        // 0, 无结果
        // n, result number
        //
        // number 从0开始计数
        //int do_miss_seg_cn2py(unsigned int number); // int是丢掉的segment的序号


    	cn2py_segment(); //为了系统初始化词典设计的
        cn2py_segment(const char * query); //应该使用的这个接口
        ~cn2py_segment();
        int cn2py_init(const char * query);

        void debug(void); // 打印元素系统信息
        void clear(void); //轻擦除. 方便复用
        void clear_miss(void); // 轻擦除miss的结果. 方便复用


    private:
    	static THash table_cn2py;
        static THash table_cn2mpy_prob;
        static int init_flag;

        char file_cn2py[32];
        char file_cn2mpy[32] ;

        char query[128];

        void load_dict(void); 
        int fullfil_hash(const char * file_name, dense_hash_map<uint64, char *, OwnHash> &table);
        int split_query_segment(const char * p);    
        
};



// 0, no result
// -1, error
// >0, result number
//int cn2py(const char * query, cn2py_segment& result );
#endif
