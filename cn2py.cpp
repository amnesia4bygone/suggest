
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cn2py.h"



THash cn2py_segment::table_cn2py = THash();
THash cn2py_segment::table_cn2mpy_prob = THash();
int cn2py_segment:: init_flag = 0;



typedef enum 
{
    CN_CHAR = 0,
    EN_CHAR,
    UNKNOWN,
}char_type;


// 判断p开头位置的字符号, 是什么类型. 
// -1, error
// 0, eng char
// 1, cn char
// 
// GBK编码
// cn_char 做的比较粗糙, 可能会有一些半个汉字的问题. 以及全角的符号. 希望前台处理干净掉. 
// TODO:
char_type is_cn_en(const char * p)
{
        if (  (unsigned char)(*p) < 128      )
                return EN_CHAR;
        else
                return CN_CHAR;
}


// 返回segment的byte数
// 这里为了简单处理, 每个汉字作为一个segment. 方便后续的处理
unsigned int search_segment_len(const char * p, char_type type)
{
        char * pos = (char*) p;
        unsigned int len = 0;
        if(type == EN_CHAR)
        {

            while( is_cn_en(pos) == EN_CHAR )
            {
                if (*pos == '\0')
                    break;
                len++;
                pos++;
            }
        }
        else
        {
                //while( is_cn_en(pos) == CN_CHAR )
                //{
                        len += 2;
                        pos += 2 ;
                //}
        }
        return len;

}


int cn2py_segment::split_query_segment(const char * p)
{

        unsigned int len = strlen(p);
        unsigned int offset = 0;

 

        while(offset < len)
        {
                char * cur_position =(char*) p + offset;

                char_type t = is_cn_en(cur_position);

                unsigned int segment_len = search_segment_len(cur_position, t);
                //printf("%s %d\n", cur_position, segment_len);

                strncpy(query_segment_str[query_segment_number], cur_position, segment_len);

                query_segment_str[query_segment_number][segment_len] = '\0';

                offset += segment_len;
                query_segment_number++;

                if (query_segment_number >= MAX_CNPY_SEGMENT_NUMBER)
                {
                    _LOG("too many segment\n");
                    return -1;
                }
        }
        return 0;
}




// value作为字符串存, 有两个考虑: 1, value比较灵活. 2, 不需要考虑dump, 动态加载char类型
int cn2py_segment::fullfil_hash(const char * file_name, THash &table)
{

        FILE * fp = fopen(file_name, "r");
        if (fp == NULL)
        {   
            _LOG("open cn2py data file error\n");
            return -1; 
        }   


        char buffer[128];
        char key[128];
        char * value;

        unsigned int len;
        int readed_lines = 0 ;


        while (!feof(fp))
        {   
                memset(buffer, 0, 128);
                memset(key, 0, 128);
                
                value = (char*)malloc(128);
                memset(value, 0, 128);

                if ( NULL == fgets(buffer, 128, fp))
                {   
                        //_LOG("fgets error");
                        free (value);
                        fclose(fp);
                        return -1;    
                }   
                buffer[127] = '\0';
                //printf("--%s---\n", buffer);

                char * sep;
                char * end;
                len = strlen(buffer);
                sep = strstr(buffer, "\t");
                end = buffer+len-2; // -2, 

                if (sep == NULL)
                    continue;

                strncpy(key, buffer, (unsigned int) (sep-buffer) );
                //unsigned int key_len = strlen(key);
                strncpy(value, sep+1, (unsigned int) (end-sep) );


                //printf("---%s---%s--\n", key, value);
                
                table[first_half_md5(key, 2)] = value;
                readed_lines++;

        }   
        //fprintf(stderr, "readed lines end %d\n",readed_lines);
        fclose(fp);
        return 0;  
}


// 全局初始化的时候使用
void cn2py_segment::load_dict(void)
{
    table_cn2py.set_empty_key(0);
    table_cn2mpy_prob.set_empty_key(0);

    fullfil_hash(file_cn2py, table_cn2py);
    //fprintf(stderr, "load dict half\n");
    fullfil_hash(file_cn2mpy, table_cn2mpy_prob);

    //fprintf(stderr, "load dict over\n");

    

    //fprintf(stderr, "test1 --%s--\n", table_cn2py[first_half_md5("朝", 2)]);

}

void cn2py_segment::debug(void)
{
    //printf(" query segment number  %d \n", query_segment_number);
    //printf("py_result_number %d\n", py_result_number);
    //printf("--%s--%s-- \n", file_cn2py, file_cn2mpy);

    for (int i=0; i<py_result_number; i++)
        printf("%d %s\n", int(strlen(py_result_str[i])), py_result_str[i]);


    for (int i=0; i<py_miss_result_number; i++)
        printf("%d %s\n", int(strlen(py_miss_result_str[i])), py_miss_result_str[i]);

}


// memset比较消耗cpu,可以优化 //TODO:
void cn2py_segment::clear(void)
{
    query_segment_number = 0;
    py_result_number = 0;
    py_miss_result_number = 0;

    memset(py_result_str, 0 , sizeof(py_result_str));
    memset(query_segment_str, 0, sizeof(query_segment_str));
    memset(py_miss_result_str, 0, sizeof(py_miss_result_str));

}


// memset比较消耗cpu,可以优化 //TODO:
void cn2py_segment::clear_miss(void)
{
    
    py_miss_result_number = 0;

    memset(py_miss_result_str, 0 , sizeof(py_miss_result_str));
}



cn2py_segment::cn2py_segment()
{
    //各个参数做一下clear
    query_segment_number = 0;
    py_result_number = 0;
    py_miss_result_number = 0;

    memset(py_result_str, 0 , sizeof(py_result_str));
    memset(query_segment_str, 0, sizeof(query_segment_str));
    memset(py_miss_result_str, 0, sizeof(py_miss_result_str));

    memset(file_cn2py, 0, 32);
    memset(file_cn2mpy, 0, 32);

    strcpy(file_cn2py, "./data/char.p");
    strcpy(file_cn2mpy, "./data/char.m");

    if (init_flag == 0)
    {
        load_dict();
        init_flag = 1;
    }
}

cn2py_segment::cn2py_segment(const char * q)
{
    //各个参数做一下clear
    query_segment_number = 0;
    py_result_number = 0;
    py_miss_result_number = 0;

    memset(py_result_str, 0 , sizeof(py_result_str));
    memset(query_segment_str, 0, sizeof(query_segment_str));
    memset(py_miss_result_str, 0, sizeof(py_miss_result_str));

    memset(query, 0, 128);
    strcpy(query, q);

    split_query_segment(query);

}


int cn2py_segment::cn2py_init(const char * q)
{
    //各个参数做一下clear
    query_segment_number = 0;
    py_result_number = 0;
    py_miss_result_number = 0;

    memset(py_result_str, 0 , sizeof(py_result_str));
    memset(query_segment_str, 0, sizeof(query_segment_str));
    memset(py_miss_result_str, 0, sizeof(py_miss_result_str));


    memset(query, 0, 128);
    strcpy(query, q);

    split_query_segment(query);

    return 0;

}

cn2py_segment::~cn2py_segment()
{
    //各个参数做一下clear
    query_segment_number = 0;
    py_result_number = 0;

    // 遍历数组, 释放
}




// 定位在单个字的转拼音好了
int cn2py_segment::do_cn2py(void)
{
    // 汉字转拼音, 其他的不动, 分别赋值给各个segment存储


    //unsigned int mpy_number = 0;


    // 按照split切分字, 输出py到py_result_str里面.
    // 这里比较理想的做法是图的深度遍历. 但较为复杂, 不如暴力输出
     int py_str_number = 1; //这个动态变化的.
    
    for (int i=0; i < query_segment_number; i++ )
    {
        //fprintf(stderr, "\n\n\n --%s-- 是当前segment\n", query_segment_str[i]);    

        if( is_cn_en(query_segment_str[i]) == EN_CHAR ) // 英文串,直接copy,简单
        {
            for(int j=0; j < py_str_number; j++)
            {
                if (   (strlen(py_result_str[j] + strlen(query_segment_str[i]) ) )  >  MAX_RESULT_BUFFER )
                    return -6;

                strncat(py_result_str[j], query_segment_str[i],  strlen(query_segment_str[i]) );
                //fprintf(stderr, "cp en segment --%s--\n", query_segment_str[i]);
            }
            continue;
        }


        THashITE it;
        //判断当前segment对应的单个汉字, 是否多音字

        unsigned long ids = first_half_md5(query_segment_str[i], 2);
        it = table_cn2mpy_prob.find( ids );

        if (it != table_cn2mpy_prob.end() ) // 是多音字
        {

            //fprintf(stderr, "%s 是个多音字\n", query_segment_str[i]);    
            // 判断多音字的个数, mpy_number
            unsigned int mpy_number = 0; //为了操作方便, 偏移offset方面.后续统计的时候要+1
            char buffer[128];
            strncpy(buffer, table_cn2py[ids], 64 );
            buffer[127] = '\0';
            //fprintf(stderr, "%s--%s %d\n", table_cn2mpy_prob[ids], table_cn2py[ids], strlen(table_cn2py[ids]));
            char * p = buffer;
            char * p_next = buffer;

            char mpy_segment[8][8];
            memset(mpy_segment, 0, sizeof(mpy_segment));

            while ( p_next )
            {
                p_next = strstr(p, "\t");

                if (p_next == NULL) //最后一个多音字copy过去.
                {
                    strcpy(mpy_segment[mpy_number], p);
                    break;
                }
                strncpy(mpy_segment[mpy_number], p, (unsigned int)(p_next - p)  );
                p = p_next+1;
                mpy_number++;
            }

            //fprintf(stderr, "多音字对应的拼音个数是: %d\n", mpy_number);
            //for (int x=0; x<= mpy_number; x++)
            // printf("    %s\n", mpy_segment[x]);

            if( ( (mpy_number+1) * py_str_number ) > 128 || mpy_number == 0 )   //避免溢出
            {
                //_LOG(" mpy couse segment overflow or mpy==0\n");
                //fprintf(stderr, "--%s--\n", query);
                return -7; //到此为止吧.
            }

            // copy 目前的buffer
            // 目前是py_str_number(4)行 ,需要 * mpy_number(3)  

            for (uint32 j=1; j<= mpy_number; j++) //第一节不需要copy
            {
                for (int k=0; k<py_str_number; k++) 
                {
                    strncpy(py_result_str[j*py_str_number + k], py_result_str[k], strlen(py_result_str[k]));
                }
            }

            // 多音字以 py_str_number作为offset, 赋值 mpy_number遍
            for (uint32 j=0; j<= mpy_number; j++)
            {
                for (int k=0; k<py_str_number; k++) 
                {

                    if ( (strlen(py_result_str[k+j*py_str_number]) + strlen( mpy_segment[j]) )> MAX_RESULT_BUFFER   )
                        return -8;

                    strncat(py_result_str[k+j*py_str_number], mpy_segment[j] , strlen(mpy_segment[j]));
                }               
            }

            // update py_str_number
            py_str_number = py_str_number * (mpy_number+1);

        }
        else
        {
            it = table_cn2py.find( ids );

            if (it == table_cn2py.end() ) //这说明,这是个中文的符号, 或者无对应的拼音啦
            {
                for(int j=0; j < py_str_number; j++)
                {
                    strncat(py_result_str[j], query_segment_str[i], strlen(query_segment_str[i]) );
                }
            }
            else
            {
                // 获取汉字对应的拼音
                for(int j=0; j < py_str_number; j++)
                {
                    if ( (strlen(py_result_str[j]) + strlen(table_cn2py[ids]) ) > MAX_RESULT_BUFFER )
                        return -9;

                    strncat(py_result_str[j], table_cn2py[ids], strlen(table_cn2py[ids]) );
                }
            }
        }


    }
    py_result_number = py_str_number;
    return py_str_number;


}

/*
int cn2py_segment::do_miss_seg_cn2py(unsigned int number=129)
{
    // 汉字转拼音, 其他的不动, 分别赋值给各个segment存储


    unsigned int mpy_number = 0;



    unsigned int py_str_number = 1; //这个动态变化的.
    
    for (int i=0; i < query_segment_number; i++ )
    {
        //fprintf(stderr, "\n\nn\n --%s-- 是当前segment\n", query_segment_str[i]); 

        if (i==number)  continue;

        if( is_cn_en(query_segment_str[i]) == EN_CHAR ) // 英文串,直接copy,简单
        {
            for(int j=0; j < py_str_number; j++)
            {
                if (   (strlen(py_result_str[j] + strlen(query_segment_str[i]) ) )  >  MAX_RESULT_BUFFER )
                    return -1;


                strncat(py_miss_result_str[j], query_segment_str[i],  strlen(query_segment_str[i]) );
                //fprintf(stderr, "cp en segment --%s--\n", query_segment_str[i]);
            }
            continue;
        }


        THashITE it;
        //判断当前segment对应的单个汉字, 是否多音字

        unsigned long ids = first_half_md5(query_segment_str[i], 2);
        it = table_cn2mpy_prob.find( ids );

        if (it != table_cn2mpy_prob.end() ) // 是多音字
        {

            //fprintf(stderr, "%s 是个多音字\n", query_segment_str[i]);  
            // 判断多音字的个数, mpy_number
            unsigned int mpy_number = 0; //为了操作方便, 偏移offset方面.后续统计的时候要+1
            char buffer[128];
            strncpy(buffer, table_cn2py[ids], 64 );
            buffer[127] = '\0';
            //fprintf(stderr, "%s--%s %d\n", table_cn2mpy_prob[ids], table_cn2py[ids], strlen(table_cn2py[ids]));
            char * p = buffer;
            char * p_next = buffer;

            char mpy_segment[8][8];
            memset(mpy_segment, 0, sizeof(mpy_segment));

            while ( p_next )
            {
                p_next = strstr(p, "\t");

                if (p_next == NULL) //最后一个多音字copy过去.
                {
                    strcpy(mpy_segment[mpy_number], p);
                    break;
                }
                strncpy(mpy_segment[mpy_number], p, (unsigned int)(p_next - p)  );
                p = p_next+1;
                mpy_number++;
            }

            //fprintf(stderr, "多音字对应的拼音个数是: %d\n", mpy_number);
            //for (int x=0; x<= mpy_number; x++)
            //  printf("    %s\n", mpy_segment[x]);

            if( ( (mpy_number+1) * py_str_number ) > 128 || mpy_number == 0 )   //避免溢出
            {
                _LOG(" mpy couse segment overflow or mpy==0 \n");
                //fprintf(stderr, "--%s--\n", query);
                return -1; //到此为止吧.
            }

            // copy 目前的buffer
            // 目前是py_str_number(4)行 ,需要 * mpy_number(3)  

            for (int j=1; j<= mpy_number; j++) //第一节不需要copy
            {
                for (int k=0; k<py_str_number; k++) 
                {
                    strncpy(py_miss_result_str[j*py_str_number + k], py_miss_result_str[k], MAX_RESULT_BUFFER-1);
                    py_miss_result_str[j*py_str_number + k][MAX_RESULT_BUFFER-1] = '\0'; //TODO 可能有半个汉字的问题...
                }
            }

            // 多音字以 py_str_number作为offset, 赋值 mpy_number遍
            for (int j=0; j<= mpy_number; j++)
            {
                for (int k=0; k<py_str_number; k++) 
                {
                    if ( (strlen(py_result_str[k+j*py_str_number]) + strlen( mpy_segment[j]) )> MAX_RESULT_BUFFER   )
                        return -1;

                    strcat(py_miss_result_str[k+j*py_str_number], mpy_segment[j] );
                }               
            }

            // update py_str_number
            py_str_number = py_str_number * (mpy_number+1);

        }
        else
        {
            it = table_cn2py.find( ids );

            if (it == table_cn2py.end() ) //这说明,这是个中文的符号, 或者无对应的拼音啦
            {
                for(int j=0; j < py_str_number; j++)
                {
                    strncat(py_result_str[j], query_segment_str[i], strlen(query_segment_str[i]) );
                }
            }
            else
            {
                // 获取汉字对应的拼音
                for(int j=0; j < py_str_number; j++)
                {
                    if ( (strlen(py_result_str[j]) + strlen(table_cn2py[ids]) ) > MAX_RESULT_BUFFER )
                        return -1;

                    strcat(py_miss_result_str[j], table_cn2py[ids] );
                }
            }
        }


    }
    py_miss_result_number = py_str_number;
    return py_str_number;


}

*/


#ifdef CN2PY_DEBUG

class test_cn
{
public:
    cn2py_segment * cp;

    test_cn()
    {
        cp = NULL;
        cp = new cn2py_segment("init usa");
    }

    ~test_cn()
    {
        if (cp != NULL)
            delete cp;
    }
};

int main(void)
{
    cn2py_segment x;

    for(int i=0; i<1; i++)
    {
        try
        {
            test_cn t;
        }
        catch(...)
        {
            ;
        }
    }
    return 1;
}
#endif

