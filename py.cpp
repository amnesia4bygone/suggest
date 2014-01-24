
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include "py.h"



THash cn2py_segment::table_cn2py = THash();
int cn2py_segment:: init_flag = 0;



// 返回字符的长度, utf8
// 1是英文
// 2,3, 4汉子
// -1, error

/*
utf8如表：   
1字节 0xxxxxxx   
2字节 110xxxxx 10xxxxxx   
3字节 1110xxxx 10xxxxxx 10xxxxxx   
4字节 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx 
*/

int  is_cn_en(const char * p)
{
        uint8 one = (unsigned char)(*p);
        
        if (  one>>7 == 0      )
                return 1;


        if ( one>>5  ==  0x6 )
                return 2;
        else if (one >> 4 == 0xe)
                return 3;
        else if (one >> 3 == 0x1e )
                return 4;
        else 
                return -1;
}



// 定位在单个字的转拼音好了
int cn2py_segment::do_cn2py(void)
{
        // start parse query to py   
        unsigned int len = strlen(query);
        unsigned int len_read = 0;
        
        string py_word;

        while (len_read < len )
        {
                char * p = query + len_read;

                int this_len = is_cn_en(p);

                if (this_len <= 0)
                {
                        printf("is cn en error\n");
                        return -1;
                }

                char buf[8];
                memset(buf, 0, 8);
                
                printf("--%s--%d\n", buf, this_len);

                strncpy( buf,  p,  this_len );

                if ( this_len > 1)
                {
                        if (py_word.length() > 0 )
                                cn_list.push_back(py_word);
                        py_word = "";
 
                        uint64 id = first_half_md5(buf, this_len );
                        printf("%lld %s, %d\n", id, buf, this_len);

                        THashITE it;
                        it = table_cn2py.find(id);
                        if (it == table_cn2py.end() )
                        {
                                printf("!!!!!!!!!!!!!!!!!miss word --%s--\n", buf);
                                return -1;
                        }
                        strcat(py_list, table_cn2py[id] ); 
                        cn_list.push_back( string( buf, this_len )  );
                }
                else
                {
                        py_word += buf;

                        strncat(py_list, buf, this_len);
                }

                len_read += this_len; 
        }
        if (py_word.length() > 0 )
            cn_list.push_back(py_word);

        return cn_list.size();
        
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

                char * tmp_ptr = strstr(sep+1, "\t");
                if (tmp_ptr == NULL)
                {
                        strncpy(value, sep+1, (unsigned int) (end-sep) );
                }
                else
                {
                        strncpy(value, sep+1,  (unsigned int) (tmp_ptr - sep - 1) );
                }


                printf("---%s---%s--\n", key, value);
                
                table[first_half_md5(key, strlen(key))] = value;
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
    fullfil_hash(file_cn2py, table_cn2py);
}

void cn2py_segment::debug(void)
{
    printf("py_list %s\n", py_list);
    for (int i=0; i<cn_list.size(); i++)
        printf("%d cn_list --%s--\n",  (int)cn_list.size(), cn_list[i].c_str() );
}





cn2py_segment::cn2py_segment()
{
    memset(file_cn2py, 0, 32);
    strcpy(file_cn2py, "./conf/char.p");

    if (init_flag == 0)
    {
        load_dict();
        init_flag = 1;
    }
}

cn2py_segment::cn2py_segment(const char * q)
{

    if (init_flag == 0)
           return;

    memset(query, 0, 128);
    strcpy(query, q);

    memset(py_list, 0,256);
}



cn2py_segment::~cn2py_segment()
{
    // 遍历数组, 释放memomy
}




#ifdef CN2PY_DEBUG


int main(void)
{
    cn2py_segment x;

    cn2py_segment one_query("anid 鸟啊囧记 我日啊! cc" );
    one_query.do_cn2py();
    one_query.debug();

    return 1;
}
#endif

