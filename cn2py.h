#ifndef __CN2PY__
#define __CN2PY__



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ���ʹ�õĵط��Ƚ϶� ���������
#include <google/dense_hash_map>
#include <openssl/md5.h>

#include <sparsehash/dense_hash_map>

#include "common_def.h"
#include "md5_64bit.h"



using google::dense_hash_map;      // namespace where class lives by default
//using std::cout;
//using std::endl;

/*
    �����ǲ��Դ���

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




// �洢����query��ƴ���Ľ��. ���ڶ����ֵĴ���, ���Զ�����.
// "���Ļ�"�Ľ��
//     "liudehua"  "liu" "de" "hua"
//     "liudihua"  "liu" "di" "hua"
//
// "h&m ����"
// 		"h&m guanwang"  "h&m "  "guan" "wang"
// 
// ���ʱ���. ��ʱ������ʹ��. �Ƕ�����,ֱ�ӳ�1, �����ֶ�Ӧ�ĸ��ʳ���������. 
class cn2py_segment
{
    public:
    	uint8 query_segment_number;
    	uint8 py_result_number;
        uint8 py_miss_result_number;

    	char py_result_str[MAX_CNPY_NUMBER][MAX_RESULT_BUFFER]; //һ��query,��෵����ô�����ƴ���Ľ��
    	char query_segment_str[MAX_CNPY_SEGMENT_NUMBER][MAX_RESULT_BUFFER]; //һ�����ֵ�ƴ�����7���ֽ�. ��һ��\0
        char py_miss_result_str[MAX_CNPY_NUMBER][MAX_RESULT_BUFFER]; //һ��query��һ��segment, ����������ƴ���Ľ��

    	float py_str_prob[MAX_CNPY_SEGMENT_NUMBER]; 

    	int do_cn2py();

        // -1, number�Ƿ� 
        // 0, �޽��
        // n, result number
        //
        // number ��0��ʼ����
        //int do_miss_seg_cn2py(unsigned int number); // int�Ƕ�����segment�����


    	cn2py_segment(); //Ϊ��ϵͳ��ʼ���ʵ���Ƶ�
        cn2py_segment(const char * query); //Ӧ��ʹ�õ�����ӿ�
        ~cn2py_segment();
        int cn2py_init(const char * query);

        void debug(void); // ��ӡԪ��ϵͳ��Ϣ
        void clear(void); //�����. ���㸴��
        void clear_miss(void); // �����miss�Ľ��. ���㸴��


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
