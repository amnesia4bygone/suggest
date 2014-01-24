
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


// �ж�p��ͷλ�õ��ַ���, ��ʲô����. 
// -1, error
// 0, eng char
// 1, cn char
// 
// GBK����
// cn_char ���ıȽϴֲ�, ���ܻ���һЩ������ֵ�����. �Լ�ȫ�ǵķ���. ϣ��ǰ̨����ɾ���. 
// TODO:
char_type is_cn_en(const char * p)
{
        if (  (unsigned char)(*p) < 128      )
                return EN_CHAR;
        else
                return CN_CHAR;
}


// ����segment��byte��
// ����Ϊ�˼򵥴���, ÿ��������Ϊһ��segment. ��������Ĵ���
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




// value��Ϊ�ַ�����, ����������: 1, value�Ƚ����. 2, ����Ҫ����dump, ��̬����char����
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


// ȫ�ֳ�ʼ����ʱ��ʹ��
void cn2py_segment::load_dict(void)
{
    table_cn2py.set_empty_key(0);
    table_cn2mpy_prob.set_empty_key(0);

    fullfil_hash(file_cn2py, table_cn2py);
    //fprintf(stderr, "load dict half\n");
    fullfil_hash(file_cn2mpy, table_cn2mpy_prob);

    //fprintf(stderr, "load dict over\n");

    

    //fprintf(stderr, "test1 --%s--\n", table_cn2py[first_half_md5("��", 2)]);

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


// memset�Ƚ�����cpu,�����Ż� //TODO:
void cn2py_segment::clear(void)
{
    query_segment_number = 0;
    py_result_number = 0;
    py_miss_result_number = 0;

    memset(py_result_str, 0 , sizeof(py_result_str));
    memset(query_segment_str, 0, sizeof(query_segment_str));
    memset(py_miss_result_str, 0, sizeof(py_miss_result_str));

}


// memset�Ƚ�����cpu,�����Ż� //TODO:
void cn2py_segment::clear_miss(void)
{
    
    py_miss_result_number = 0;

    memset(py_miss_result_str, 0 , sizeof(py_miss_result_str));
}



cn2py_segment::cn2py_segment()
{
    //����������һ��clear
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
    //����������һ��clear
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
    //����������һ��clear
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
    //����������һ��clear
    query_segment_number = 0;
    py_result_number = 0;

    // ��������, �ͷ�
}




// ��λ�ڵ����ֵ�תƴ������
int cn2py_segment::do_cn2py(void)
{
    // ����תƴ��, �����Ĳ���, �ֱ�ֵ������segment�洢


    //unsigned int mpy_number = 0;


    // ����split�з���, ���py��py_result_str����.
    // ����Ƚ������������ͼ����ȱ���. ����Ϊ����, ���籩�����
     int py_str_number = 1; //�����̬�仯��.
    
    for (int i=0; i < query_segment_number; i++ )
    {
        //fprintf(stderr, "\n\n\n --%s-- �ǵ�ǰsegment\n", query_segment_str[i]);    

        if( is_cn_en(query_segment_str[i]) == EN_CHAR ) // Ӣ�Ĵ�,ֱ��copy,��
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
        //�жϵ�ǰsegment��Ӧ�ĵ�������, �Ƿ������

        unsigned long ids = first_half_md5(query_segment_str[i], 2);
        it = table_cn2mpy_prob.find( ids );

        if (it != table_cn2mpy_prob.end() ) // �Ƕ�����
        {

            //fprintf(stderr, "%s �Ǹ�������\n", query_segment_str[i]);    
            // �ж϶����ֵĸ���, mpy_number
            unsigned int mpy_number = 0; //Ϊ�˲�������, ƫ��offset����.����ͳ�Ƶ�ʱ��Ҫ+1
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

                if (p_next == NULL) //���һ��������copy��ȥ.
                {
                    strcpy(mpy_segment[mpy_number], p);
                    break;
                }
                strncpy(mpy_segment[mpy_number], p, (unsigned int)(p_next - p)  );
                p = p_next+1;
                mpy_number++;
            }

            //fprintf(stderr, "�����ֶ�Ӧ��ƴ��������: %d\n", mpy_number);
            //for (int x=0; x<= mpy_number; x++)
            // printf("    %s\n", mpy_segment[x]);

            if( ( (mpy_number+1) * py_str_number ) > 128 || mpy_number == 0 )   //�������
            {
                //_LOG(" mpy couse segment overflow or mpy==0\n");
                //fprintf(stderr, "--%s--\n", query);
                return -7; //����Ϊֹ��.
            }

            // copy Ŀǰ��buffer
            // Ŀǰ��py_str_number(4)�� ,��Ҫ * mpy_number(3)  

            for (uint32 j=1; j<= mpy_number; j++) //��һ�ڲ���Ҫcopy
            {
                for (int k=0; k<py_str_number; k++) 
                {
                    strncpy(py_result_str[j*py_str_number + k], py_result_str[k], strlen(py_result_str[k]));
                }
            }

            // �������� py_str_number��Ϊoffset, ��ֵ mpy_number��
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

            if (it == table_cn2py.end() ) //��˵��,���Ǹ����ĵķ���, �����޶�Ӧ��ƴ����
            {
                for(int j=0; j < py_str_number; j++)
                {
                    strncat(py_result_str[j], query_segment_str[i], strlen(query_segment_str[i]) );
                }
            }
            else
            {
                // ��ȡ���ֶ�Ӧ��ƴ��
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
    // ����תƴ��, �����Ĳ���, �ֱ�ֵ������segment�洢


    unsigned int mpy_number = 0;



    unsigned int py_str_number = 1; //�����̬�仯��.
    
    for (int i=0; i < query_segment_number; i++ )
    {
        //fprintf(stderr, "\n\nn\n --%s-- �ǵ�ǰsegment\n", query_segment_str[i]); 

        if (i==number)  continue;

        if( is_cn_en(query_segment_str[i]) == EN_CHAR ) // Ӣ�Ĵ�,ֱ��copy,��
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
        //�жϵ�ǰsegment��Ӧ�ĵ�������, �Ƿ������

        unsigned long ids = first_half_md5(query_segment_str[i], 2);
        it = table_cn2mpy_prob.find( ids );

        if (it != table_cn2mpy_prob.end() ) // �Ƕ�����
        {

            //fprintf(stderr, "%s �Ǹ�������\n", query_segment_str[i]);  
            // �ж϶����ֵĸ���, mpy_number
            unsigned int mpy_number = 0; //Ϊ�˲�������, ƫ��offset����.����ͳ�Ƶ�ʱ��Ҫ+1
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

                if (p_next == NULL) //���һ��������copy��ȥ.
                {
                    strcpy(mpy_segment[mpy_number], p);
                    break;
                }
                strncpy(mpy_segment[mpy_number], p, (unsigned int)(p_next - p)  );
                p = p_next+1;
                mpy_number++;
            }

            //fprintf(stderr, "�����ֶ�Ӧ��ƴ��������: %d\n", mpy_number);
            //for (int x=0; x<= mpy_number; x++)
            //  printf("    %s\n", mpy_segment[x]);

            if( ( (mpy_number+1) * py_str_number ) > 128 || mpy_number == 0 )   //�������
            {
                _LOG(" mpy couse segment overflow or mpy==0 \n");
                //fprintf(stderr, "--%s--\n", query);
                return -1; //����Ϊֹ��.
            }

            // copy Ŀǰ��buffer
            // Ŀǰ��py_str_number(4)�� ,��Ҫ * mpy_number(3)  

            for (int j=1; j<= mpy_number; j++) //��һ�ڲ���Ҫcopy
            {
                for (int k=0; k<py_str_number; k++) 
                {
                    strncpy(py_miss_result_str[j*py_str_number + k], py_miss_result_str[k], MAX_RESULT_BUFFER-1);
                    py_miss_result_str[j*py_str_number + k][MAX_RESULT_BUFFER-1] = '\0'; //TODO �����а�����ֵ�����...
                }
            }

            // �������� py_str_number��Ϊoffset, ��ֵ mpy_number��
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

            if (it == table_cn2py.end() ) //��˵��,���Ǹ����ĵķ���, �����޶�Ӧ��ƴ����
            {
                for(int j=0; j < py_str_number; j++)
                {
                    strncat(py_result_str[j], query_segment_str[i], strlen(query_segment_str[i]) );
                }
            }
            else
            {
                // ��ȡ���ֶ�Ӧ��ƴ��
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

