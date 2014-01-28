#ifndef __CN2PY__
#define __CN2PY__



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 这个使用的地方比较多 单独存好了
#include <google/dense_hash_map>
#include <openssl/md5.h>

#include <sparsehash/dense_hash_map>


#include <vector>
#include <string>

#include "common_def.h"
#include "md5_64bit.h"



using google::dense_hash_map;      // namespace where class lives by default
using namespace std;



int  is_cn_en(const char * p);


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
        char  py_list[256];        
        vector<string> cn_list;        


    	cn2py_segment(); //为了系统初始化词典设计的
        cn2py_segment(const char * query); //应该使用的这个接口
        ~cn2py_segment();

        void debug(void); // 打印元素系统信息 
        int do_cn2py(void);

    private:
    	static THash table_cn2py;
        static int init_flag;

        char file_cn2py[32];
        char query[128];

        void load_dict(void); 
        int fullfil_hash(const char * file_name, dense_hash_map<uint64, char *, OwnHash> &table);
        
};



#endif
