#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <time.h>
#include <errno.h>
#include <netinet/in.h>
#include <strings.h>
#include <signal.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#include <vector>
#include <string>
#include <algorithm>

#include "py.h"
#include "md5_64bit.h"
#include "pool.h"
#include "contents.h"





#define BACKLOG 10

#define QY_TIME gettimeofday(&old_tv, NULL)



using namespace std;


typedef dense_hash_map<uint64, contents *, OwnHash> CHash;
typedef dense_hash_map<uint64, contents *, OwnHash>::iterator CHashITE;


const unsigned int MAX_LEN = 1024;
const unsigned int THREAD_NUMBER =  24;

unsigned int g_port = 2020; 
PendingPool g_workpool;

int debug = 0;

const unsigned int MAX_INDEX_SIZE = 10000000;
const unsigned int MAX_QUERY_SIZE = 1000000;
CHash g_index =  CHash(MAX_INDEX_SIZE);
THash g_querys = THash(MAX_QUERY_SIZE);

 
const char response_head[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Length: ";


void urldecode(char *pszDecodedOut, size_t nBufferSize, const char *pszEncodedIn)
{

    uint32 len = strlen(pszEncodedIn);
    if ( len==0 )
        return;

    memset(pszDecodedOut, 0, nBufferSize);

    enum DecodeState_e
    {
        STATE_SEARCH = 0, ///< searching for an ampersand to convert
        STATE_CONVERTING, ///< convert the two proceeding characters from hex
    };

    DecodeState_e state = STATE_SEARCH;

    for(unsigned int i = 0; i < len; ++i)
    {
        switch(state)
        {
        case STATE_SEARCH:
            {
                if(pszEncodedIn[i] != '%')
                {
                    strncat(pszDecodedOut, &pszEncodedIn[i], 1);
                    //assert(strlen(pszDecodedOut) < nBufferSize);
                    //printf("%s\n", pszDecodedOut);
                    break;
                }

                //printf("change converting\n");
                // We are now converting
                state = STATE_CONVERTING;
            }
            break;

        case STATE_CONVERTING:
            {
                // Conversion complete (i.e. don't convert again next iter)
                state = STATE_SEARCH;

                // Create a buffer to hold the hex. For example, if %20, this
                // buffer would hold 20 (in ASCII)
                char pszTempNumBuf[3] = {0};
                strncpy(pszTempNumBuf, &pszEncodedIn[i], 2);

                // Ensure both characters are hexadecimal
                bool bBothDigits = true;

                for(int j = 0; j < 2; ++j)
                {
                    if(!isxdigit(pszTempNumBuf[j]))
                        bBothDigits = false;
                }

                if(!bBothDigits)
                    break;

                // Convert two hexadecimal characters into one character
                int nAsciiCharacter;
                sscanf(pszTempNumBuf, "%x", &nAsciiCharacter);

                // Ensure we aren't going to overflow
                //assert(strlen(pszDecodedOut) < nBufferSize);

                // Concatenate this character onto the output
                strncat(pszDecodedOut, (char*)&nAsciiCharacter, 1);
                //printf("%s\n", pszDecodedOut);
                // Skip the next character
                i++;
            }
            break;
        }
    }
}



int tcplisten(int port, int type)
{

    int listenfd;
    struct sockaddr_in servaddr,tempaddr;
    
    
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) == -1){
            perror("socket");
             exit(1);
    } 
    

    int on=1;  
    if((setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)  
    {  
        perror("setsockopt failed");  
        exit(EXIT_FAILURE);  
    }


    bzero(&servaddr,sizeof(servaddr));
    //if (type == 0)
    servaddr.sin_family = AF_INET;
    //else
    //    servaddr.sin_family = AF_LOCAL;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons( uint16(g_port));
   
    if (bind(listenfd,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1)
    {
             perror("bind");
             exit(1);
    }

    socklen_t templen = sizeof(struct sockaddr);
    if (getsockname(listenfd, (struct sockaddr *)&tempaddr,&templen) == -1){
            perror("getsockname");
             exit(1);
    }
    printf("Server is listening on port %d\n", ntohs(tempaddr.sin_port));
    
    
    if(listen(listenfd,BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }  

    return listenfd;
}


int tcpaccept(int listenfd)
{
    //printf("tcp accept in\n");
    int connfd;
    struct sockaddr_in cliaddr;
    socklen_t clilen;  

    clilen = sizeof(cliaddr);

    if((connfd = accept(listenfd,(struct sockaddr *)&cliaddr,&clilen)) == -1)
    {    
        if(errno == EINTR)
        {
            return -1;
        }
        else{
            perror("accept error");
            return -1;
        }
    }

    return connfd;

}

/* 
    这里补充http协议的包头即可 
    should deal with typedef struct request


    returns:
        -1, socket error
        0, parse error
        1, ok
 */
int tcpread_query(int connfd, char * request_query)
{
    //printf("tcp readquery in\n");
    char buf[MAX_LEN];
    memset(buf, 0, MAX_LEN);


    int n=0;
    unsigned int read_len = 0;
    int read_flag = 1;
    while( read_flag < 5 && read_flag )
    {
        n = 0;
        n =read(connfd,  buf+read_len, sizeof(buf) - read_len -1 ) ;

        if (n>0)
            read_len += n;

        if (read_flag == 4)
        {
            printf("%s\n", buf);
            return -1;
        }

        if (errno == EINTR)
        {
            read_flag++;
            continue;
        }

        if ( strstr(buf, "\r\n\r\n") )
            read_flag = 0;
        else
            read_flag++;
    }
    buf[MAX_LEN -1] = '\0';

    if(debug) printf("%s\n", buf);

    if (read_len > (unsigned int)(MAX_LEN-10) || read_len == 0 )
    {
        printf("buf too long or = 0\n");
        return -1;
    }

    if (strlen(buf) == 0)
    {
        printf("buf = 0\n");
        return -1;        
    }




    char * p_head = strstr(buf, "query=");   
    if (p_head == NULL)  
    {
        printf("not find query=\n");
        return -1;
    }
    
    unsigned int query_len = 0;
    char * p_tab = strstr(p_head + 6, " "); // 6是query=的长度
    if (p_tab == NULL   ) 
    {
        p_tab = strstr(p_head + 6, " ");
        if (p_tab == NULL ) 
        {
            printf("parse query error\n");
            return -1;
        }
    }
    query_len = (unsigned int )(p_tab - p_head - 6);
    if (query_len > 255 || query_len == 0 )
    {
        printf(" query_len > 255 or = 0\n");
        return 0;
    }

    char query_quote[256];
    memset(query_quote, 0, 256);
    strncpy(query_quote, p_head+6, query_len); 
    
    // urldecode
    char buf_unquote[MAX_LEN];
    memset(buf_unquote, 0, MAX_LEN);
    urldecode(buf_unquote, MAX_LEN, query_quote);
    buf_unquote[MAX_LEN -1] = '\0';

     

    strncpy(request_query, buf_unquote, strlen(buf_unquote) );
    
    //printf("\n------------------%s-----\n", request_query);

    return 1;
}




/*
    2. 返回数据：
    2.1 类型： json串
        2.2 格式： {"prompt":[提示词],"result":提示词数目}，如果无相应提示词时提示词为空，提示词数目为0。
        2.3 示例：
             有提示词情况：{"prompt": ["12200805", "12 \u79cb\u88c5", "12 \u79cb\u51ac", "1225", "12one", "12 \u5bf8 \u7535\u8111\u5305", "120d \u6253\u5e95", "123456789", "12 \u676f \u6587\u80f8 \u805a\u62e2", "123"], "result": 10}
             无提示词情况：{"prompt": [], "result": 0} 
 */
int tcpsendmsg(int connfd,  vector<string>&results)
{
    //printf("tcp sendmsg  in\n");
    int n = 0;
    uint32 result_number = results.size();

    char send_buf[MAX_LEN];
    memset(send_buf, 0, MAX_LEN);
    strcpy(send_buf, response_head);




    unsigned int len = 0;
    char buf[512];
    char tmp_buf[MAX_LEN];
    memset(tmp_buf, 0, MAX_LEN);
    unsigned int nLen  = 0;


    strcpy(tmp_buf, "{\"prompt\": [");
    // {“name”:“刘德华”, “key2”:1},
    for (uint32 i=0; i<result_number; i++)
    {
        memset(buf, 0, 512);

        snprintf(buf, 511, "\"%s\"", results[i].c_str());
                
        buf[511] = '\0';

        strcat(tmp_buf, buf);
        if (i != result_number -1)
            strcat(tmp_buf, ",");
    }

    strcat(tmp_buf, "], \"result\": ");
        memset(buf, 0, 512);
        snprintf(buf, 511, "%d",result_number);
        buf[511] = '\0';
    strcat(tmp_buf, buf);

    strcat(tmp_buf, "}\r\n");




    nLen = strlen(tmp_buf);

    memset(buf, 0, 512);
    sprintf(buf, "%d\r\n\r\n", nLen);
    strcat(send_buf, buf );

    len = strlen(send_buf);
    memcpy( send_buf + len, tmp_buf, nLen );


    if(debug) printf("send_buf --%s--\n", send_buf);

    if((n = write(connfd, send_buf, len+nLen))<0)
    {
        perror("send error");
        return -1;
    }


    return 0;
}


void request_server(void)
{
    int listenfd = tcplisten(0, 0);
    int connfd;
    int offset;


    while (1) 
    {

        connfd = tcpaccept(listenfd);
        vector<string> result;

        if (connfd > 0) 
        {
            if ((offset = g_workpool.insert_item(connfd)) == -1)
            {
                printf("PendingPool insert error\n");
                tcpsendmsg(connfd, result);
                close(connfd);
                printf("insert items : overflow!\n");
            } 
            else
            {
                if (g_workpool.queue_in(offset) < 0) 
                {
                    g_workpool.work_reset_item(offset, false);
                    printf( "queen_in: overflow!\n");
                    printf("PendingPool queue in error\n");
                }
            }

        } 
        else
        {
            printf( "Accept client error!");
        }
    }
}




void * child_main(void* )
{   
    int     ret = 0;    // return value
    int     handle, queuelength, wait_time;
    int     client = -1;



    while (1) 
    {

        try
        {
            vector<string> results;
            vector<string> first_type_results;
            vector<string> second_type_results;

            if (!(ret = g_workpool.work_fetch_item(handle, client, queuelength, wait_time)))
                continue;

            char cmd[1024];
            memset(cmd , 0, 1024);
            // -1 means socket error, 0 means bad request
            int ret = tcpread_query(client, cmd) ;


            if (1 != ret) // bad request
            {
                tcpsendmsg(client, results);
                g_workpool.work_reset_item(handle, false);
                continue;
            }
            
            uint64 id = first_half_md5(cmd, strlen(cmd) );
            CHashITE ite = g_index.find(id);
            if (ite != g_index.end() )
            {
                contents * p = g_index[id];

                MATCH_TYPE first_type = TYPE_NULL;

                for (unsigned int i =0; i < p->used_num; i++ )
                {
                    
                    {

                        printf( "%d--%lld--%s--%d--%d--%lld---%d\n" , 
                             i , 
                             p->lists[i].id, 
                             g_querys[ p->lists[i].id ] , 
                             p->lists[i].doota, 
                             p->lists[i].search,
                             p->lists[i].unique_id,
                             p->lists[i].type );
                    }

                    if (first_type == TYPE_NULL)
                        first_type = p->lists[i].type;

                    int dup_flag = 0;
                    for (unsigned int j =0; j<i; j++)
                    {
                        if ( p->lists[i].unique_id == p->lists[j].unique_id )
                        {
                            dup_flag = 1;
                            break;
                        }
                    }

                    // 过滤原串
                    if ( strlen(g_querys[ p->lists[i].id]) == strlen(cmd)    &&   0 == strcmp(cmd, g_querys[ p->lists[i].id])  )
                        continue; 



                    if (dup_flag == 0 )
                    {
                        if (  p->lists[i].type == first_type )
                        {
                            first_type_results.push_back( string(g_querys[ p->lists[i].id] ) );
                        }
                        else
                        {
                            second_type_results.push_back( string(g_querys[ p->lists[i].id] ) );
                        }
                    }

                }
            }

            unsigned int first_used = 0;
            unsigned int first_size = first_type_results.size();

            for(unsigned int i=0; i<6 && first_used < first_size; i++, first_used++)
            {
                results.push_back( first_type_results[i] );
            }

            unsigned int second_used = 0;
            unsigned int second_size = second_type_results.size();
            for(unsigned int i=0; i< 10 - first_used && second_used < second_size; i++, second_used++)
            {
                results.push_back( second_type_results[i] );
            }

            // 不足10个, 队列1补齐
            if (results.size() < 10)
            {
                unsigned int used = results.size();
                for(unsigned int i= used; i<10 && first_used < first_size; i++, first_used++)
                {
                    results.push_back( first_type_results[i] );
                }                
            }




            tcpsendmsg(client, results);
            g_workpool.work_reset_item(handle, false);

        }
        catch (...) 
        {
            // 
            g_workpool.work_reset_item(handle, false);
        }
    }

    
    return NULL;
}    



uint32 strip_blank ( char * query, char * new_query, uint32 len )
{
        char * p = query;
        char * dest = new_query;
        uint32 blank_number = 0;
        for (uint32 i =0; i< len; i++)
        {
                if (*p == ' ' || *p == '\'' || *p == '.' || *p == ',' )
                {
                        p++;
                        blank_number++;
                }
                else
                {
                        *dest = *p;
                        p++;
                        dest++;

                }
        }
        *dest = '\0';
        return blank_number;
}



unsigned int insert_one_query(char * query, unsigned int doota, unsigned int search )
{
    vector<string> prefixs;
    vector<MATCH_TYPE> types;
    
    cn2py_segment segment(query);
    segment.do_cn2py();
    if(debug) segment.debug();
    
    char * p_head = (char*) (segment.py_list  );
    unsigned int query_len = strlen(p_head);

    uint64 query_id = first_half_md5(query, strlen(query) ); 
    char * new_query = (char * ) malloc(64);
    memset(new_query, 0, 64);
    strncpy(new_query, query, 63);

    g_querys[query_id] = new_query; 

    {
        // 前缀 py 索引
        for(unsigned int i=1 ; i<=query_len; i++ )
        {
            
            string one(p_head, i );

            prefixs.push_back(one);
            types.push_back(PY_PRE);

            //printf("%%pre  %s\n",  one.c_str() );

        }

        // 中缀 py 索引

        char * p_blank = strstr( p_head,  " ");
        while(p_blank != NULL)
        {
            char * p_mid_py_query = p_blank+1;
            unsigned int mid_py_query_len = strlen( p_mid_py_query );

            for(unsigned int i = 1; i<=mid_py_query_len; i++)
            {
                string one(p_mid_py_query, i );

                prefixs.push_back(one);
                types.push_back(PY_MID);

                //printf("%%mid  %s\n",  one.c_str() );            
            }

            p_blank = strstr( p_mid_py_query, " " );

        }

        // 前缀 汉字索引
        for (unsigned int i=1; i<= segment.cn_list.size(); i++)
        {
            string one;
            for (unsigned int j =0; j<i; j++)
            {
                one += segment.cn_list[j];
            }
            prefixs.push_back(one);
            types.push_back(CN_PRE);
            if(debug) printf("%%cn pre  --%s--\n",  one.c_str() );

        }


        // 中缀, 汉子索引
        unsigned int offset = 0;
        unsigned int current_offset = 0;
        unsigned int cn_len = segment.cn_list.size();

        while(current_offset < cn_len)
        {
            if (  0 == strncmp(segment.cn_list[current_offset].c_str() , " ", 1 ) )
                break;

            current_offset++;
        }
        current_offset++; // skip blank
        offset = current_offset;

        while(offset < cn_len)
        {
            for(unsigned int i=1; i<=cn_len - offset; i++  )
            {
                string one;
                for(unsigned int j=0; j<i; j++)
                    one += segment.cn_list[offset+j];
                prefixs.push_back(one);
                types.push_back(CN_MID);
                if(debug) printf("%%cn mid pre  --%s--\n",  one.c_str() );
            }

            while(current_offset < cn_len)
            {
                if (  0 == strncmp(segment.cn_list[current_offset].c_str() , " ", 1 ) )
                    break;

                current_offset++;
            }  
            offset = ++current_offset;     
        }
    }



    if (debug)
        for (unsigned int i =0; i<prefixs.size(); i++)
            printf("---%s\n", prefixs[i].c_str() );



    char query_no_blank[64];
    memset(query_no_blank, 0, sizeof(query_no_blank) );
    strip_blank(query, query_no_blank, 64);
    uint64 unique_id = first_half_md5(query_no_blank, strlen(query_no_blank) );

    for (unsigned int i=0; i<prefixs.size(); i++)
    {
        CHashITE ite;

        uint64 key_id = first_half_md5(prefixs[i].c_str(), strlen(prefixs[i].c_str() )  );

        ite = g_index.find(key_id);
        contents * one = NULL;
        if (ite == g_index.end() )
        {
            one = new contents();
            g_index[key_id] = one;
        }
        else
        {
            one = g_index[key_id];
        }
        int ret  = one->insert(query_id, doota, search, unique_id, types[i] );

        
        if (debug) printf("%d, %s, %s, %d, %d\n", ret, query, prefixs[i].c_str(), doota, search );
        if (debug) one->debug();
    }
    
    return prefixs.size();
}


unsigned int build_index(void)
{
    g_index.set_empty_key(0);
    g_querys.set_empty_key(0);


    //unsigned int len;
    int readed_lines = 0 ;

    FILE * fp = fopen("conf/sug.query", "r");
    if (fp == NULL)
    {   
        _LOG("open cn2py data file error\n");
        return -1; 
    }   


    char buffer[256];
    char key[128];
    char value[128];
    //char * p = NULL;

    uint32 read_keys_num= 0;

    while (!feof(fp))
    {   
        memset(buffer, 0, 256);
        memset(key, 0, 128);
        memset(value, 0, 128); 

        int doota = -1;
        int search = -1;
       

        if ( NULL == fgets(buffer, 256, fp))
        {   
              
                break;   
        }   
        buffer[255] = '\0';
        //printf("--%s---\n", buffer);


        // 过滤渣数据
        char * p1 = strstr(buffer, "^");
        char * p2 = strstr(buffer, "$");
        if (p1 != NULL && p2 != NULL)
            continue;

        char * sep;
        char * end;
        //len = strlen(buffer);


        sep = strstr(buffer, "\t");
        if (sep == NULL)
            continue;

        strncpy(key, buffer, (unsigned int) (sep-buffer) );

        doota = atoi( sep+1 );

        end = strstr(sep+1, "\t");
        if (end == NULL)
            continue;
        search = atoi(end+1 );           
        

        if (debug )
            printf("---%s---%d--%d--\n", key, doota, search);
        

        // use it for skep "keywords    doota_num   user_search_times"
        if ( doota == 0 && search == 0 )
            continue;

        read_keys_num += insert_one_query(key, doota, search);

        readed_lines++;
        if (debug)
        {
            if (readed_lines > 10)
                return 0;
        }
    }   
    printf( "readed lines end %d %d\n",readed_lines, read_keys_num);
    fclose(fp);
    return 0;      

}

int main(int argc,char* argv[])
{
    //signal(SIGUSR2,  SIG_IGN);
    //signal(SIGUSR1,  SIG_IGN);
    signal(SIGPIPE,  SIG_IGN);

    
    cn2py_segment g_dcit_cs; // init dict just here

    printf("cs init over\n" );




    build_index();



    pthread_t pt[THREAD_NUMBER]; 
    memset(pt, 0, sizeof(pt));


    for (unsigned int i=0; i< THREAD_NUMBER; i++)
    {
        if(pthread_create(&pt[i], NULL, child_main, NULL ) != 0)
        {
            perror("pthread_create error \n");
            return -1;
        }
    }



    request_server();

    for (unsigned int i=0; i< THREAD_NUMBER; i++)
    {
        pthread_join(pt[i], NULL);
    }

    return 0;
}



