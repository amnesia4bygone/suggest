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

#include "cn2py.h"
#include "md5_64bit.h"
#include "pool.h"





#define BACKLOG 10

#define QY_TIME gettimeofday(&old_tv, NULL)

using namespace std;

const unsigned int MAX_LEN = 1024;
const unsigned int THREAD_NUMBER =  24;

const unsigned int MAX_RESULT_LIST = 500;

const unsigned int LM_N_GRAM =  3;
const unsigned int IME_N_GRAM =  3;
const double       LM_THRESHOLD = 50;



const unsigned int MAX_ED_THRESHOLD = 2;


int debug = 0;
int cr_flag  = 0;

int running_flag = 1;

unsigned int g_port = 8000; 

DataWarehouse * g_dwh = NULL;

PendingPool g_workpool;

fuzzy * g_fuzzy = NULL;

Mlang *mlang = NULL;
Lattice* lattice = NULL;

my_log * p_log = NULL;
my_log * p_status_log = NULL;
my_log * p_error_log = NULL;

//code_convert * g_u2g = NULL;

//code_convert * g_g2u = NULL;

PYLM * g_pylm = NULL;

unsigned int g_reload_flag = 0;

const char response_head[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Length: ";

const char response_err_head[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Length: 0\r\n\r\n";


void print_time( struct timeval & old_tv, char * buf, char * query )
{
    struct timeval cur_tv;
    int sleep_usec;

    if (debug>=2)
    {
        gettimeofday(&cur_tv, NULL);
        sleep_usec = (cur_tv.tv_sec-old_tv.tv_sec)*1000000+(cur_tv.tv_usec-old_tv.tv_usec);

        //if (sleep_usec > 20000)
        printf("%s ",  buf);
        printf(" %s--%d\n", query, sleep_usec);
        gettimeofday(&old_tv, NULL);    
    }
}


// listen on port
// type means AF_LOCAL == 1 or AN_INET == 0
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
    //p_log->write_log("tcp accept in\n");
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
    //if(debug>=1) printf("accept success\n");

    return connfd;

}

/* 
    这里补充http协议的包头即可 
    should deal with typedef struct request

    test use: query \t number \t url \t title \t abs

    returns:
        -1, socket error
        0, parse error
        1, ok
 */
int tcpread_query(int connfd, request &cmd)
{
    //p_log->write_log("tcp readquery in\n");
    char buf[MAX_LEN];
    memset(buf, 0, MAX_LEN);
    int ret = 0;


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
            p_error_log->write_log(buf);
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

    p_status_log->write_log(buf);

    if (read_len > (unsigned int)(MAX_LEN-10) || read_len == 0 )
    {
        p_error_log->write_log("buf too long or = 0\n");
        return -1;
    }

    if (strlen(buf) == 0)
    {
        p_error_log->write_log("buf = 0\n");
        return -1;        
    }
    if (debug >= 2)  printf("receive form client:--%s--\n",buf);




    char * p_head = strstr(buf, "query=");   
    if (p_head == NULL)  
    {
        p_error_log->write_log("not find query=\n");
        return -1;
    }
    
    unsigned int query_len = 0;
    char * p_tab = strstr(p_head + 6, "&"); // 6是query=的长度
    if (p_tab == NULL   ) 
    {
        p_tab = strstr(p_head + 6, " ");
        if (p_tab == NULL ) 
        {
            p_error_log->write_log("parse query error\n");
            return -1;
        }
    }
    query_len = (unsigned int )(p_tab - p_head - 6);
    if (query_len > 255 || query_len == 0 )
    {
        p_error_log->write_log(" query_len > 255 or = 0\n");
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
    
    // utf8 --> gb18030
    unsigned short unicode_buf[MAX_LEN];
    char tmp_buf[64];
    memset(tmp_buf, 0, sizeof(tmp_buf));
    unsigned int len2 = MAX_LEN;


    unsigned int len1 = strlen(buf_unquote);
    ret = ConvToUnicode(buf_unquote, len1, unicode_buf, MAX_LEN, qwww_code_utf8);
    if (ret == -1 )
    {
        //p_log->write_log("ERROR HEPPEN\n", 13);
        p_error_log->write_log("parse error\n");
        return -1;
    }

    len2  = ConvFrUnicode(unicode_buf, ret, tmp_buf, 63, qwww_code_gbk);
    tmp_buf[63] = '\0';
    query_len = strlen(tmp_buf);
    if (query_len > 60 || query_len == 0 )
    {
        p_error_log->write_log(" query_len > 255 or = 0\n");
        return 0;
    }
    p_status_log->write_log(tmp_buf);
    p_status_log->write_log("\n");


    
    p_tab  =  strstr(buf, "&pb_flag=");  
    if (p_tab == NULL)  
        cmd.pb_flag = 0;
    else
        cmd.pb_flag = 1; 

    // 这里做一下归一化
    if (debug >= 2)  printf("ok query--%s--\n", tmp_buf);
    strncpy(cmd.source, tmp_buf, strlen(tmp_buf));
    normalize(tmp_buf, cmd.query);


    cmd.url =1;
    cmd.title =1;
    cmd.abs = 1;
    cmd.result_number = 100;

    if (debug >= 2)  printf("cmd info:--%s--%s---%d--%d--%d--%d--%d\n",cmd.query, 
                                cmd.source, cmd.result_number, cmd.url, cmd.title, cmd.abs, cmd.pb_flag);
    //p_log->write_log("tcp readquery out\n");
    return 1;
}


int tcpsendmsg(int connfd,  vector<correct_result_t>&results, int pb_flag = 0)
{
    //p_log->write_log("tcp sendmsg  in\n");
    int n = 0;
    uint32 result_number = results.size();

    char send_buf[MAX_LEN];
    memset(send_buf, 0, MAX_LEN);
    strcpy(send_buf, response_head);

    char buf_g2u[MAX_LEN];
    memset(buf_g2u, 0, MAX_LEN);
    unsigned int len = 0;
    char buf[512];
    char tmp_buf[MAX_LEN];
    memset(tmp_buf, 0, MAX_LEN);
    unsigned int nLen  = 0;


    strcpy(tmp_buf, "[");
    // {“name”:“刘德华”, “key2”:1},
    for (uint32 i=0; i<result_number; i++)
    {
        memset(buf, 0, 512);

        snprintf(buf, 511, "{\"name\":\"%s\", \"confidence\":%d, \"freq\":%d, \"cooc\":%d, \"type\":%d, \"score\":%lld, \"lm\":%lf}", 
                           results[i].query, results[i].confidence, results[i].freq, results[i].cooc, results[i].type, results[i].score, results[i].lm);
        buf[511] = '\0';

        strcat(tmp_buf, buf);
        if (i != result_number -1)
            strcat(tmp_buf, ",");
    }
    strcat(tmp_buf, "]\r\n");

    unsigned short unicode_buf[MAX_LEN];
    uint32 ret = ConvToUnicode(tmp_buf, strlen(tmp_buf), unicode_buf, MAX_LEN, qwww_code_gbk);
    ConvFrUnicode(unicode_buf, ret, buf_g2u, MAX_LEN, qwww_code_utf8);
    nLen = strlen(buf_g2u);


    
    //
    // over write buf_g2u. tmp_buf also used for log
    //
    if (pb_flag == 1)
    {
        Candidates candidates;
        candidates.set_type(FULLQUERYPINYIN);


        char utf8_buf[512];
        memset(utf8_buf, 0, 512);
        unsigned short unicode_buf[512];
        memset(unicode_buf, 0, 512);

        uint32 ret = 0;
        Feature *pFeature = NULL;

        if (results.size() == 0) // for too long query, return null result
        {
            string oquery("*");
            candidates.set_oquery( oquery );
        }
        else
        {
            ret = ConvToUnicode((results[0]).query, strlen((results[0]).query), unicode_buf, 511, qwww_code_gbk);
            ConvFrUnicode(unicode_buf, ret, utf8_buf, 511, qwww_code_utf8);
            utf8_buf[511] = '\0';
            string oquery(utf8_buf);
            candidates.set_oquery( oquery );


            pFeature = candidates.add_ofeature();
            if (results[0].type == CR_PRE_ORDER_BAIKE || results[0].type == CR_PRE_ORDER_ENG )
            {
                pFeature->set_fid("isInDict");
                pFeature->set_fval(1);
            }
            else
            {
                pFeature->set_fid("isInDict");
                pFeature->set_fval(0);            
            }
        }

        if (results.size() >= 2)
        {
            for (unsigned int i=1; i<results.size(); i++)
            {
                memset(unicode_buf, 0, 512);
                memset(utf8_buf, 0, 512);

                uint32 ret = ConvToUnicode((results[i]).query, strlen((results[i]).query), unicode_buf, 511, qwww_code_gbk);
                ConvFrUnicode(unicode_buf, ret, utf8_buf, 511, qwww_code_utf8);
                utf8_buf[511] = '\0';

                Candidate *pCandidate = candidates.add_candidate();
                pCandidate->set_cquery(utf8_buf);

                pFeature = pCandidate->add_cfeature();
                pFeature->set_fid("freq");
                pFeature->set_fval(results[i].freq);

                pFeature = pCandidate->add_cfeature();
                pFeature->set_fid("cooc");
                pFeature->set_fval(results[i].cooc);

                pFeature = pCandidate->add_cfeature();
                pFeature->set_fid("lm");
                pFeature->set_fval(results[i].lm);

                pFeature = pCandidate->add_cfeature();
                pFeature->set_fid("confidence");
                pFeature->set_fval(results[i].confidence);

                pFeature = pCandidate->add_cfeature();
                pFeature->set_fid("type");
                pFeature->set_fval(results[i].type);

                pFeature = pCandidate->add_cfeature();
                if ( results[i].type == CR_HERD_MANUAL || results[i].type ==  URL_ED ) 
                {
                    pFeature->set_fid("rule_flag");
                    pFeature->set_fval(1);                    
                }
                else
                {
                    pFeature->set_fid("rule_flag");
                    pFeature->set_fval(0);
                }
            }
        } 
        memset(buf_g2u, 0, MAX_LEN);
        nLen = candidates.ByteSize();
        if (nLen > MAX_LEN) 
            return 0;
        if (!candidates.SerializeToArray(buf_g2u, nLen)) {
            return 0;
        }       

        //string str_tmp = candidates.Utf8DebugString();
        //printf("result=%s\n", str_tmp.c_str());
    }



    //len = strlen(buf_g2u);

    memset(buf, 0, 512);
    sprintf(buf, "%d\r\n\r\n", nLen);
    strcat(send_buf, buf );

    len = strlen(send_buf);
    memcpy( send_buf + len, buf_g2u, nLen );
    //strcat(send_buf, buf_g2u);

    //len = strlen(send_buf);
    if((n = write(connfd, send_buf, len+nLen))<0)
    {
        perror("send error");
        return -1;
    }

    if (results.size() > 1)
    {
        p_log->write_log( tmp_buf );
        //printf("ML:%s %lf %s %lf\n", results[0].query, p1, results[1].query, p2);
    }

    return 0;
}




int tcpsenderror(int connfd)
{
    int n = 0;
    int len = strlen(response_err_head);

    if((n = write(connfd,response_err_head, len))<0)
    {
        perror("send error");
        return -1;
    }

    return 0;
}



int is_head_manual(uint64 queryid, char * query)
{
    DataWarehouse * dwh = g_dwh;
    int ret = dwh->get_head_manual_result(queryid, query);
    if (ret > 0 ) 
        return 1;
    else
        return 0;
}

int is_post_manual(uint64 queryid, char * query)
{
    DataWarehouse * dwh = g_dwh;
    int ret = dwh->get_manual_result(queryid, query);
    if (ret > 1 ) 
        return 2;
    else if  (ret == 1)
        return 1;
    else
        return 0;
}



uint32  get_freq(uint64 id)
{
    DataWarehouse * dwh = g_dwh;

    uint32 freq = dwh->get_freq(id);

    return freq;
}



uint32  get_cooc(char * query1, char * query2)
{
    DataWarehouse * dwh = g_dwh;
    char buffer[128];
    memset(buffer, 0 , sizeof(buffer));

    //sprintf(buffer, "%s\t%s", query1, query2);
    strcpy(buffer, query1);
    strcat(buffer, "\t");
    strcat(buffer, query2);
    buffer[127] = '\0';

    uint64 id = first_half_md5(buffer, strlen(buffer));
    uint32 freq = dwh->get_cooc_freq(id);

    return freq;
}


/*
    return value:
        0, no baike
        1, baike
*/
int  is_baike(uint64 queryid)
{
    DataWarehouse * dwh = g_dwh;

    uint32 freq = dwh->get_baike(queryid);

    return freq;
}

int  is_location(uint64 queryid)
{
    DataWarehouse * dwh = g_dwh;

    uint32 freq = dwh->get_location(queryid);

    return freq;
}

int  is_eng_word(uint64 queryid)
{
    DataWarehouse * dwh = g_dwh;

    uint32 freq = dwh->get_eng_word(queryid);

    return freq;
}

int  is_corp_name(uint64 queryid)
{
    DataWarehouse * dwh = g_dwh;

    uint32 freq = dwh->get_corp_name(queryid);

    return freq;
}


//
// 这里不对短query做检查
// 因为短地名, 会在is_baike策略里面直接命中,不会走到这里. 默认都是3个汉字起步的
// 这里为了赶工, 对于diff的元素是否英文没有太区分
//      英文query比例少, 增加逻辑复杂性. 所以忽略
//
//  0, 表示diff不是地名
//  1, 表示diff是地名导致的
// 
//  DIRTY: z这里做个恶心的事情, 看diff的地方是不是停止词
// 
uint32 diff_in_location(vector<string> & w1_list, vector<string> & w2_list)
{
    uint32 len1 =  w1_list.size();
    uint32 len2 =  w2_list.size();

    if (len1 <=3 || len2 <= 3)
        return 0;

    vector<string> big;
    vector<string> small;
    uint32 len_big = 0;
    uint32 len_small = 0;
    if (len1 >= len2)
    {
        big = w1_list;
        small = w2_list;
         len_big = len1;
         len_small = len2;
    }
    else
    {
        big = w2_list;
        small = w1_list;
         len_big = len2;
         len_small = len1;
    }

    char buffer[128];
    memset(buffer, 0 , sizeof(buffer));
    
    uint32 diff_position = 0;
    int flag = 0;

    vector<string>::iterator it;
    for (uint32 i=0 ; i < len_big; i++)
    {
        it = find(small.begin(), small.end(), big[i]);

        if (it == small.end() )
        {
            flag = 1;
            break;
        }
        diff_position++;
    }   

    if (flag == 0)
        return 0; // means 两个query字符集和一样的. 只是顺序不同.

    //printf("-%d-\n", diff_position);
    uint64 id1 = 0;
    uint64 id2 = 0;
    char query1[64];
    char query2[64];

    char stop_word[32];

    memset(stop_word, 0, 32);
    memset(query1, 0, 64);
    memset(query2, 0, 64);

    if (diff_position==0)
    {
        strcpy(query1, big[0].c_str()  );
        strcat(query1, big[1].c_str()  );
        strcpy(stop_word, big[0].c_str());
        query1[63] = '\0';
        //printf("-%s-%s-\n", query1, query2);
        id1 = first_half_md5(query1, strlen(query1) );
    }
    else if (diff_position==len_big-1)
    {
        strcpy(query1, big[len_big-2].c_str());
        strcat(query1, big[len_big-1].c_str()  );

        strcpy(stop_word, big[len_big-1].c_str());

        query1[63] = '\0';
        //printf("-%s-%s-\n", query1, query2);
        id1 = first_half_md5(query1, strlen(query1) );
    }
    else
    {
        strcpy(query1, big[diff_position-1].c_str());
        strcat(query1, big[diff_position].c_str()  );
        query1[63] = '\0';
        id1 = first_half_md5(query1, strlen(query1) );

        strcpy(query2, big[diff_position].c_str());
        strcat(query2, big[diff_position+1].c_str()  );
        query2[63] = '\0';
        id2 = first_half_md5(query2, strlen(query2) );

        strcpy(stop_word, big[diff_position].c_str());
        //printf("-%s-%s-\n", query1, query2);
    }

    //printf("%llx  %llx \n", id1, id2);

    if ( is_location(id1) ||  is_location(id2)   )
    {
        if(debug>=2) printf("-%s-%s-\n", query1, query2);
        if(debug>=2) printf("hit location\n");
        return 1;
    }
    else
    {
        if (debug>=2) printf("-%s-%s-\n", query1, query2);


        // 停止词(语气词)比较
        if (      !strncmp(stop_word, "么", 2) || !strncmp(stop_word, "嘛", 2) 
              ||  !strncmp(stop_word, "吗", 2) || !strncmp(stop_word, "哪", 2)
              ||  !strncmp(stop_word, "呀", 2) || !strncmp(stop_word, "啊", 2)
              ||  !strncmp(stop_word, "唉", 2) || !strncmp(stop_word, "哎", 2)
              ||  !strncmp(stop_word, "上", 2) || !strncmp(stop_word, "下", 2)
           )
        {
            return 1;
        }

        return 0;
    }
}



int inner_filter_by_lm (double d1, double d2, int len )
{
    double punishment  = 0;
    if ( len <= 8 )
        punishment = 0;
    else if (len <= 12)
        punishment = 1;
    else if (len <= 16)
        punishment = 3;
    else
        punishment = 5;

    if ( punishment < ( d1 - d2 ) )
        return 1;
    else
        return 0;
}
//
// 不同策略应该严谨程度不一样
// 第一期先上再说. 1说明需要过滤
//
int filter_by_lm(request & cmd, correct_result_t & result)
{
    double point =  mlang_get_flogp_sentence(mlang, result.query, LM_N_GRAM);

    result.lm = point;
    unsigned int len = strlen( cmd.query );

    return inner_filter_by_lm( point, cmd.lm, len );
    /*
    double punishment  = 0;
    if ( len <= 8 )
        punishment = 0;
    else if (len <= 12)
        punishment = 1;
    else if (len <= 16)
        punishment = 3;
    else
        punishment = 5;

    if ( punishment < ( point - cmd.lm ) )
        return 1;
    else
        return 0;
    */
}



int word_is_similar(string & w1, string &w2)
{
    //printf("word_is_similar  %s %s\n", w1.c_str(), w2.c_str());

    DataWarehouse * dwh = g_dwh;

    char buf[128];
    memset(buf, 0, 128);
    sprintf(buf, "%s\t%s", w1.c_str(), w2.c_str());
    uint64 id =  first_half_md5(buf, strlen(buf)) ;
    //printf("word_is_similar1  %llx\n",id );
    if ( 0 != dwh->get_close_word(id) )
        return 1;
    //printf("word_is_similar2  %llx\n",id );
    memset(buf, 0, 128);
    sprintf(buf, "%s\t%s", w2.c_str(), w1.c_str());
    id =  first_half_md5(buf, strlen(buf)) ; 
    if ( 0 != dwh->get_close_word(id) )
        return 1;
    //printf("word_is_similar3  %llx\n",id );

    cn2py_segment  cp1("init use");  
    cn2py_segment  cp2("init use");  

    cp1.cn2py_init(w1.c_str() );
    int ret = cp1.do_cn2py();

    cp2.cn2py_init(w2.c_str() );
    ret = cp2.do_cn2py();

    ED ed;
    unsigned int min_ed = 9999;
    for (unsigned int i=0; i< cp1.py_result_number; i++)
    {
        string s1(cp1.py_result_str[i]);
        for (unsigned int j=0; j< cp2.py_result_number; j++)
        {
            string s2(cp2.py_result_str[j] );
            //printf("word_is_similar  %s %s\n", s1.c_str(), s2.c_str());
            unsigned int cur_ed = ed.minDistance(s1, s2);

            // hu qi ed is 2, but not reasonalle. should punish
            if ( s1.length() == 2 && s2.length() == 2 )
                cur_ed++;
            //printf("word_is_similar  %d\n", cur_ed );
            if (cur_ed < min_ed)
                min_ed = cur_ed;
        }    
    }
    //printf("word_is_similar  %d\n", min_ed );
    if (min_ed <= MAX_ED_THRESHOLD)
        return 1;
    else
        return 0;
}


int build_result_freq_cooc(char* query, vector<correct_result_t> &results)
{
    char * p_one_result;
    DataWarehouse * dwh = g_dwh;
    for (uint32 j=0; j< results.size(); j++)
    {
        p_one_result = results[j].query;

        int len_one_result = strlen(p_one_result);

        if (len_one_result > 50 )
        {
            fprintf(stderr, "one query toooooo long");
            continue;
        }


        uint64 id_one_result = first_half_md5( p_one_result, len_one_result );
        uint32 freq_one_result = dwh->get_freq(id_one_result);

        char tmp_buf[128];
        memset(tmp_buf, 0, sizeof(tmp_buf));
        strcpy(tmp_buf, query);
        strcat(tmp_buf, "\t");
        strcat(tmp_buf, p_one_result);
        uint64 cooc_id = first_half_md5( tmp_buf, strlen(tmp_buf) );
        uint32 cooc = dwh->get_cooc_freq(cooc_id);

        results[j].cooc = cooc;
        results[j].freq = freq_one_result;

    }    
    return 0;
}




uint32 find_best_for_py(vector<correct_result_t> &source, vector<correct_result_t> &result, correct_result_t & one)
{
    uint32 result_size = source.size();

    if (debug >= 2) debug_msg(source);
    vector<correct_result_t> tmp_result;
    for (uint32 i=0; i< result_size; i++)
    {
        if (source[i].confidence != 0)
        {
            tmp_result.push_back( source[i] );
        }
    }
    if (debug >= 2)  printf("((((((((((((((((((((((((((((((((((((((((((((((((((((");
    if (debug >= 2)  debug_msg(tmp_result);

    float max_result_pylm = 99999;
    stable_sort (tmp_result.begin(), tmp_result.end(), results_sort_by_score);
    if (debug >= 2)  printf("tmp result size %ld\n", tmp_result.size());
    cn2py_segment  cp("init use");  
    for(uint32 i=0; i< tmp_result.size(); i++ )
    {
        cp.cn2py_init(tmp_result[i].query);
        int ret = cp.do_cn2py();
        if (ret < 0)
        {
            if (debug >= 2)  printf("cn2py init error\n");
            tmp_result[i].pylm = 99999;
            continue;
        }

        float one_tmp_pylm = 99999;
        for (int j=0; j< cp.py_result_number ; j++)
        {
            fuzzy * pf =  g_fuzzy;
            fuzzy_py_results lists;

            if (debug >= 2)  printf("py result is %s\n", cp.py_result_str[j]);
            pf->check_py( cp.py_result_str[j], lists );
            if (debug >= 2)  pf->debug(lists);
            float tmp_pylm = g_pylm->get_pylm_score(lists);
            if (debug >= 2)  printf("tmp_pylm is %f\n", tmp_pylm);
            if (tmp_pylm < one_tmp_pylm)
                one_tmp_pylm = tmp_pylm;
        }

        tmp_result[i].pylm = one_tmp_pylm;
        if (debug >= 2)  printf("one_tmp_pylm is %f\n", one_tmp_pylm);
        if (one_tmp_pylm < max_result_pylm )
                max_result_pylm = one_tmp_pylm;
    }
    if (debug >= 2)  debug_msg(tmp_result);

    // it means should not do py change
    if (debug >= 2)  printf("cmd pylm %f\n", one.pylm);
    if (max_result_pylm - one.pylm > 10 || one.pylm < 20)
    {
        for(uint32 i=0; i< tmp_result.size(); i++ )
        {
            if (tmp_result[i].type != PY_TOTAL)
                tmp_result[i].score = 1;
        }
    }

    stable_sort (tmp_result.begin(), tmp_result.end(), results_sort_by_score);
    for(uint32 i=0; i< tmp_result.size() && i<3; i++ )
    {
        if (tmp_result[i].score > 1)
            result.push_back(tmp_result[i]);
    }
    if (debug >= 2)  printf("((((((((((((((((((((((((((((((((((((((((((((((((((((");
    //debug_msg(result);
    return result.size();

}



int build_result_score( vector<correct_result_t> &results)
{
    //char * p_one_result;
    //DataWarehouse * dwh = g_dwh;
    for (uint32 j=0; j< results.size(); j++)
    {
        int cooc = results[j].cooc+ 1;
        int freq = results[j].freq;

        double point =  mlang_get_flogp_sentence(mlang, results[j].query, LM_N_GRAM);

        results[j].lm = point;

        uint64 score = uint64 ( freq * cooc / point );

        if (score < 4)
            score = 4;

        if (results[j].type == PY_TOTAL )
            results[j].score = score;
        else if (results[j].type == PY_ED_WRONG )
            results[j].score = score/10;
        else
            results[j].score = score/2;


    }    
    return 0;
}



int get_correct_py(char * query, vector<correct_result_t> &results, result_type type)
{
    DataWarehouse * dwh = g_dwh;
    //char send_buf[38400]; // 600个结果的槽位, 为了预防溢出, 判断限制在500哥 
    cn2py_segment  cp("init use");  
    //memset(send_buf, 0, sizeof(send_buf));

    int tmp_ret_num = 0;
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
    {
        if (debug >= 2)   printf("choice 0, %s  %d do cn2py error\n",query, ret);
            result_number = 0;
    }
    else
    {

        for (int i=0; i< cp.py_result_number && result_number < 500; i++)
        {
            if (debug >= 2)  printf("one py result --%s--\n", cp.py_result_str[i]);
            uint64 id =  first_half_md5(cp.py_result_str[i], strlen(cp.py_result_str[i])) ;
            if(debug>=2)  printf("id is --%llx--\n", id);

            tmp_ret_num = dwh->get_py2query(id, results, type);

            if (tmp_ret_num > 0)
                result_number += tmp_ret_num;

            if( result_number >= 500)
            {
                //fprintf(stderr, "%s cause send_buf overwrite \n", query);
                break; // TODO 
            }

        }

        build_result_freq_cooc(query, results);
        build_result_score(results);
    }
    
    cp.clear();
    return 0;
}



//
// 为了传递一些信息, 这里传值cmd
// 更合理的做法是单独去弄 .不过 ,so what
//
int get_correct_py_fuzzy(request & cmd, vector<correct_result_t> &results, result_type type)
{
    char * query = cmd.query;
    struct timeval old_tv;
    QY_TIME;


    query_type type1;
    parse_query(query, type1);

    // check_py could not hold number, puncation. so  short solution
    if (type1.number == 1 || type1.blank == 1 || type1.puncation == 1)
        return 0;

    //p_log->write_log(" fuzzy in\n");
    DataWarehouse * dwh = g_dwh;
    //cn2py_segment  cp("init use");  
    
    int tmp_ret_num = 0;
    int result_number = 0; 

    (cmd.cp)->cn2py_init(query);
    int ret = (cmd.cp)->do_cn2py();
    if (ret < 0)
    {
        //p_log->write_log("fuzzy docn2py error\n");
        return 0;
    }

    float pylm = 99999;

    for( int cp_offset=0; cp_offset< (cmd.cp)->py_result_number && result_number < 500; cp_offset++)
    {
        query_type tmp_type;
        parse_query((cmd.cp)->py_result_str[cp_offset], tmp_type);
        if (tmp_type.cn == 1 )
            return 0;

        fuzzy * pf =  g_fuzzy;

        //vector<vector<string> > lists;
        fuzzy_py_results lists;
        int num_err_fragment = pf->check_py(cmd.cp->py_result_str[cp_offset], lists);
        print_time( old_tv, "check py", cmd.cp->py_result_str[cp_offset] );

        if (debug>=2 )
            pf->debug(lists);



        set<string> ed1_lists;
        vector<string> ed2_lists;
        vector<string> err_lists;
        vector<string> err_retry_lists;


        set<string> err_retry_lists_backup;
        set<string>::iterator it;


        if (num_err_fragment == 0)
        {
            float tmp_pylm = g_pylm->get_pylm_score(lists);
            if (tmp_pylm < pylm )
                pylm = tmp_pylm;

            //p_log->write_log("fuzzy num err ==0  in\n");
            //for(unsigned int i=0; i<lists.size(); i++)
            for(unsigned int i=0; i<lists.lists_size; i++)
            {
                pf->generate_fuzzy_query(lists, i, ed1_lists, FUZZY_COMMON);
            }    
            print_time( old_tv, "num_err_fragment == 0  generate_fuzzy_query", (cmd.cp)->py_result_str[cp_offset] );

            //for(unsigned int i=0; i<ed1_lists.size(); i++)
            for( it=ed1_lists.begin(); it!=ed1_lists.end(); it++ )
            {
                uint64 id =  first_half_md5((*it).c_str(), strlen((*it).c_str()) );
                tmp_ret_num = dwh->get_py2query(id, results, type);

                if (tmp_ret_num > 0)
                    result_number += tmp_ret_num;

                if( result_number >= 500)
                    break;
            }

            //print_time( old_tv, "num_err_fragment == 0  get py2queyr", (cmd.cp)->py_result_str[cp_offset] );


            /*
            // if there is no result.... ed2, //TODO, on depend need
            for(unsigned int i=0; i<lists.size(); i++)
            {
                pf->generate_fuzzy_query(lists[i], ed2_lists, FUZZY_ED2);
            }


            
            for(unsigned int j=0; j<ed1_lists.size(); j++)
                printf("ed1--%s--\n",   ed1_lists[j].c_str() );              
            for(unsigned int j=0; j<ed2_lists.size(); j++)
                printf("ed2--%s--\n",   ed2_lists[j].c_str() );
                */
        }
        else
        {
            //p_log->write_log("fuzzy err num != 0\n");
            for(unsigned int i=0; i<lists.lists_size; i++)
            {
                pf->generate_fuzzy_query(lists, i, ed1_lists, FUZZY_COMMON);
            } 
            //print_time( old_tv, "num_err_fragment != 0  generate_fuzzy_query", cp.py_result_str[cp_offset] );

            fuzzy_py_results tmp;
            //for(unsigned int j=0; j<ed1_lists.size(); j++)
            for( it=ed1_lists.begin(); it!=ed1_lists.end(); it++ )
            {
                tmp.clear();
                //if(0 == pf->check_py(ed1_lists[j].c_str(), tmp)  )
                if(0 == pf->check_py((*it).c_str(), tmp)  )
                {
                    //err_lists.push_back( ed1_lists[j] );
                    err_lists.push_back( *it );
                }
            }
            //print_time( old_tv, "num_err_fragment != 0 check py", cp.py_result_str[cp_offset] );

            if (err_lists.size() == 0) // err ed1 is not enough for here
            {
                fuzzy_py_results err_tmp;
                //for(unsigned int j=0; j<ed1_lists.size(); j++)
                for(it=ed1_lists.begin(); it!=ed1_lists.end(); it++)
                {
                    err_tmp.clear();

                    pf->check_py((*it).c_str(), err_tmp);

                    for(unsigned int m=0; m<err_tmp.lists_size; m++ )
                        pf->generate_fuzzy_query( err_tmp, m,  err_retry_lists_backup,  FUZZY_COMMON );
                }            

                fuzzy_py_results check_err;
                //for(unsigned int m=0; m<err_retry_lists_backup.size(); m++)
                for(it=err_retry_lists_backup.begin(); it!=err_retry_lists_backup.end(); it++)
                {
                    check_err.clear();
                    if ( 0 == pf->check_py((*it).c_str(), check_err)  ) 
                            err_retry_lists.push_back(*it);
                    //printf("err ed2--%s--\n",   err_retry_lists[m].c_str() );
                }
                //print_time( old_tv, "num_err_fragment != 0 size == 0", cp.py_result_str[cp_offset] );
            }
            else
            {
                for(unsigned int i=0; i<err_lists.size(); i++)
                {
                    uint64 id =  first_half_md5(err_lists[i].c_str(), strlen(err_lists[i].c_str()) );
                    tmp_ret_num = dwh->get_py2query(id, results, type);

                    if (tmp_ret_num > 0)
                        result_number += tmp_ret_num;

                    if( result_number >= 500)
                        break;
                }
                //print_time( old_tv, "num_err_fragment != 0 size != 0", cp.py_result_str[cp_offset] );
                /*
                for(unsigned int m=0; m<err_lists.size(); m++)
                {
                    printf("err ed1--%s--\n",   err_lists[m].c_str() );
                }  */          
            }
            
        }
        //print_time( old_tv, "new_loop over", cp.py_result_str[cp_offset] );
    }
    build_result_freq_cooc(query, results);
    build_result_score(results);

    cmd.pylm = pylm;

    return 0;

}


//
// 这里高效的做法是query原串是汉字, 就cn2py切分, 效率高. 
// 但要多写逻辑, 一期利用通用的fuzzy去切拼音
//
int get_correct_im( char* query, vector<correct_result_t> &results, result_type type)
{
    query_type type1;
    parse_query(query, type1);

    if ( type1.number == 1 ||  type1.cn == 1 || type1.puncation == 1 || type1.blank == 1)
        return 0;

    unsigned int len1 = strlen(query);
    if (len1 <= 6 )
        return 0;

    cn2py_segment  cp("init use");  
    
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
        return 0;

    unsigned int correct_py_num = 0;
    vector<string> py_candidate; // syl seperate with blank
    // 获取所有py的组合. 空格分割. 方便计算pylm
    for( int cp_offset=0; cp_offset< cp.py_result_number && result_number < 500 ; cp_offset++)
    {

        fuzzy * pf =  g_fuzzy;

        fuzzy_py_results lists;
        //vector<string> err_lists;
        //int num_err_fragment = pf->check_py(cp.py_result_str[cp_offset], lists);
        pf->check_py(cp.py_result_str[cp_offset], lists);


            for(unsigned int i=0; i<lists.lists_size ; i++)
            {
                char pingyin_segment[128];
                memset(pingyin_segment, 0, 128);

                for (unsigned int j=0; j<lists.list_num[i]; j++)
                {
                    strcat(pingyin_segment, lists.lists[i][j] );
                    if (j != lists.list_num[i] -1 )
                        strcat(pingyin_segment, " ");
                }
                string tmp(pingyin_segment);
                py_candidate.push_back(tmp);

                correct_py_num++;
                //float tmp = g_pylm->get_pylm_score(pingyin_segment);
                //pylm_list.push_back(tmp);
            }

            set<string> ed1_lists;
            for(unsigned int i=0; i<lists.lists_size; i++)
            {
                pf->generate_fuzzy_query(lists, i, ed1_lists, FUZZY_COMMON);
            } 

            fuzzy_py_results tmp;
            set<string>::iterator it;

            for( it=ed1_lists.begin(); it!=ed1_lists.end(); it++ )
            {
                tmp.clear();
                if(0 == pf->check_py((*it).c_str(), tmp)  )
                {
                    char pingyin_segment[128];

                    if ( tmp.lists_size >= 32 )
                        continue; 
                          
                    for(unsigned int m=0; m<tmp.lists_size ; m++)
                    {
                        memset(pingyin_segment, 0, 128);   
                        for (unsigned int j=0; j<tmp.list_num[m]; j++)
                        {
                            strcat(pingyin_segment, tmp.lists[m][j] );
                            if (j != tmp.list_num[m] -1 )
                                strcat(pingyin_segment, " ");
                        }
                        string tmp(pingyin_segment);
                        py_candidate.push_back(tmp);
                    }
                }
            }

    }

    vector<float> pylm_list;
    // 计算pylm
    for( unsigned int i=0; i< py_candidate.size(); i++ )
    {
        float tmp = g_pylm->get_pylm_score(py_candidate[i].c_str() );

        pylm_list.push_back(tmp);        
    }

    float pylm_min = 9999;
    for( unsigned int i=0; i< py_candidate.size(); i++ )
    {
        if (pylm_min > pylm_list[i] )
            pylm_min = pylm_list[i];
    }
    
    float pylm_threshold = pylm_min * 1.5;
    for(unsigned int i=0; i<py_candidate.size(); i++)
    {
        //printf("final py select %s %f\n", py_candidate[i].c_str(), pylm_list[i]);
        if ( pylm_list[i] > pylm_threshold)
            continue;

        char pingyin_segment[128];
        memset(pingyin_segment, 0, 128);
        strcpy( pingyin_segment, py_candidate[i].c_str() );

        for (unsigned int j=0; j<strlen(pingyin_segment); j++)
        {
            if ( pingyin_segment[j] == ' ' )
                pingyin_segment[j] =  '|' ;
        }


        correct_result_t one;

        //struct timeval old_tv;
        //gettimeofday(&old_tv, NULL);

        char buf[128];
        memset(buf, 0, 128);
        int rtn = lattice_calculate(lattice, mlang, IME_N_GRAM , pingyin_segment, 128, buf);

        //print_time( old_tv, "lattice_calculate", one.query );

        if (rtn == -1)
            continue;
        if (strlen(buf) > 38)
            continue;

        double tmp_lm = mlang_get_flogp_sentence(mlang, buf, LM_N_GRAM);

        // here is simple ways to do this in stratery I
        strcpy(one.query, buf);
        one.confidence = 1;
        strcpy(one.msg, "im stratery I");
        one.type = type;
        one.lm = tmp_lm;

        if (i>=correct_py_num)
            one.lm = tmp_lm * 1.2;

        results.push_back(one);
    }    
    
    build_result_freq_cooc(query, results);

    cp.clear();
    return 0;

}


int get_correct_py_add_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    DataWarehouse * dwh = g_dwh;
    cn2py_segment  cp("init use");  


    int tmp_ret_num = 0;
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
    {
        if (debug >= 2)   printf("choice 0, %s  %d do cn2py error\n",query, ret);
        result_number = 0;
    }
    else
    {
        for (int i=0; i< cp.py_result_number && result_number < 500; i++)
        {
            char * p = cp.py_result_str[i];

            uint64 id =  first_half_md5(p, strlen(p)) ;
            tmp_ret_num = dwh->get_pyed2query(id, results, type);

            if (tmp_ret_num > 0)
                result_number += tmp_ret_num;

            if( result_number >= 500)
            {
                //fprintf(stderr, "%s cause send_buf overwrite \n", query);
                break; // TODO 
            }

        }

        build_result_freq_cooc(query, results);
        build_result_score(results);
    }
    return 0;
}





int get_correct_py_miss_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    DataWarehouse * dwh = g_dwh;
    cn2py_segment  cp("init use");  

    int tmp_ret_num = 0;
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
    {
        if (debug >= 1)   printf("choice 0, %s  %d do cn2py error\n",query, ret);
        result_number = 0;
    }
    else
    {

        for (int i=0; i< cp.py_result_number && result_number < 500; i++)
        {
            for (uint32 j = 0; j< strlen(cp.py_result_str[i]) ; j++)
            {
                
                char new_query[MAX_RESULT_BUFFER];
                memset(new_query, 0, sizeof(new_query));

                char * p = cp.py_result_str[i];

                if (char_is_in_puncation_and_number(p[j]) )
                    continue;

                if (j == 0)
                {
                    strncpy(new_query, p+1, strlen(p) -1  );  
                }
                else if (j== strlen(p)-1)
                {
                    strncpy(new_query, p, strlen(p) -1  );  
                }
                else
                {
                    strncpy(new_query, p, j); 
                    strncat(new_query, p+j+1, strlen(p) - j-1);
                }

                uint64 id =  first_half_md5(new_query, strlen(new_query)) ;

                tmp_ret_num = dwh->get_py2query(id, results, type);

                if (tmp_ret_num > 0)
                    result_number += tmp_ret_num;

                if( result_number >= 500)
                {
                    //fprintf(stderr, "%s cause send_buf overwrite \n", query);
                    break; // TODO 
                }
            }
        }
        build_result_freq_cooc(query, results);
        build_result_score(results);
    }
    
    cp.clear();

    return 0;
}



int get_correct_py_reverse_ed(char* query,  vector<correct_result_t> &results, result_type type)
{
    DataWarehouse * dwh = g_dwh;
    char send_buf[38400]; // 600个结果的槽位, 为了预防溢出, 判断限制在500哥 
    cn2py_segment  cp("init use");  
    memset(send_buf, 0, sizeof(send_buf));

    int tmp_ret_num = 0;
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
    {
        if (debug >=2)   printf("choice 0, %s  %d do cn2py error\n",query, ret);
        result_number = 0;
    }
    else
    {

        for (int i=0; i< cp.py_result_number && result_number < 500; i++)
        {
            for (unsigned int j = 0; j< strlen(cp.py_result_str[i]) -1 ; j++)
            {
                
                char new_query[MAX_RESULT_BUFFER];
                memset(new_query, 0, sizeof(new_query));

                char * p = cp.py_result_str[i];
                if (j==0)
                {
                    if (  char_is_in_puncation_and_number(p[0]) || char_is_in_puncation_and_number(p[1]) )
                        continue;

                    new_query[1] = p[0];
                    new_query[0] = p[1];
                    strncat(new_query, p+2, strlen(p) -2 );
                }

                else
                {
                    if (  char_is_in_puncation_and_number(p[j]) || char_is_in_puncation_and_number(p[j+1]) )
                        continue;

                    strncpy(new_query, p, j); 
                    new_query[j] = p[j+1];
                    new_query[j+1] = p[j];
                    strncat(new_query, p+j+2, strlen(p) - j -2);
                }
                uint64 id =  first_half_md5(new_query, strlen(new_query)) ;

                tmp_ret_num = dwh->get_py2query(id, results, type);

                if (tmp_ret_num > 0)
                    result_number += tmp_ret_num;

                if( result_number >= 500)
                {
                    //fprintf(stderr, "%s cause send_buf overwrite \n", query);
                    break; // TODO 
                }
            }

        }
        build_result_freq_cooc(query, results);
        build_result_score(results);

    }
    
    cp.clear();

    return 0;
}



int get_correct_py_wrong_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    struct timeval old_tv;
    QY_TIME;

    DataWarehouse * dwh = g_dwh;
    char send_buf[38400]; // 600个结果的槽位, 为了预防溢出, 判断限制在500哥 
    cn2py_segment  cp("init use");  
    memset(send_buf, 0, sizeof(send_buf));

    int tmp_ret_num = 0;
    int result_number = 0; 

    cp.cn2py_init(query);
    int ret = cp.do_cn2py();
    if (ret < 0)
    {
        if (debug >=2 )   printf("choice 0, %s  %d do cn2py error\n",query, ret);
        result_number = 0;
    }
    else
    {

        for (int i=0; i< cp.py_result_number && result_number < 500; i++)
        {
            for (unsigned int j = 0; j< strlen(cp.py_result_str[i]) ; j++)
            {
                
                char new_query[MAX_RESULT_BUFFER];
                memset(new_query, 0, sizeof(new_query));

                char * p = cp.py_result_str[i];

                if (char_is_in_puncation_and_number(p[j]) )
                    continue;

                if (j == 0)
                {
                    strncpy(new_query, p+1, strlen(p) -1  );  
                }
                else if (j== strlen(p)-1)
                {
                    strncpy(new_query, p, strlen(p) -1  );  
                }
                else
                {
                    strncpy(new_query, p, j); 
                    strncat(new_query, p+j+1, strlen(p) - j-1);
                }
                //printf("new_query --%s--%d--\n", new_query, result_number);
                print_time( old_tv, "get_correct_py_wrong_ed: build new py", cp.py_result_str[i] );


                uint64 id =  first_half_md5(new_query, strlen(new_query)) ;

                tmp_ret_num = dwh->get_pyed2query(id, results, type);

                if (tmp_ret_num > 0)
                    result_number += tmp_ret_num;

                if( result_number >= 500)
                {
                    //fprintf(stderr, "%s cause send_buf overwrite \n", query);
                    break; // TODO 
                }
                print_time( old_tv, "get_correct_py_wrong_ed: build result", cp.py_result_str[i] );
            }
            print_time( old_tv, "get_correct_py_wrong_ed: one loop over", cp.py_result_str[i] );

        }
        build_result_freq_cooc(query, results);
        build_result_score(results);
    }

    cp.clear();

    return 0;
}


// 整串级别的片段纠错
// 只做第一层. 重查放入整体的重查策略去看.
int get_correct_segment_total(request & cmd, vector<correct_result_t> &results, result_type type)
//int get_correct_segment_total(char* query, vector<correct_result_t> &results, result_type type)
{
    query_type type1;
    parse_query(cmd.query, type1);

    // 只针对 汉字 + en, 或者数字+en的query纠错
    // example: 
    //      4399 keyilianwangwanma
    //      苍井空 shiribenlaoshima
    if  ( !( type1.en == 1 &&  ( type1.cn == 1 || type1.number == 1 ) ) )
        return 0;

    string py;
    string pre;
    string post;

    struct timeval old_tv;
    QY_TIME;

    get_query_py_part(cmd.query, py, pre, post);
    print_time( old_tv, "get_query_py_part", cmd.query );
    if (debug>=2) printf("segment frames is --%s--\n", py.c_str());
    uint64 id =  first_half_md5(py.c_str(), strlen(py.c_str() ) )  ;


    //vector<vector<string> > english_lists;
    //int english_flag =  g_fuzzy->check_english( py.c_str() , english_lists) ;

    // if it is py segment

        fuzzy_py_results lists;
    
        int py_error_frag = g_fuzzy->check_py( py.c_str() , lists);
    
        if ( py_error_frag >= 2 )
        {
            if (debug>=2) printf("check py error");
            return 0;
        }
    

    // py segment hit rules, should not do segment correct
    if( is_baike(id) || is_eng_word(id) )//|| is_head_manual(id, tmp_buf) )
    {
        if (debug>=2) printf("hit baike or eng_word");
        return 0;
    }

    uint32 freq = get_freq(id);

    char tmp_buf[64]; memset(tmp_buf, 0, 64);


    print_time( old_tv, "g_fuzzy->check_py", cmd.query );

    vector<correct_result_t> tmp_results;
    request tmp_cmd;
    strcpy(tmp_cmd.query, py.c_str() );

    get_correct_py(tmp_cmd.query, tmp_results, type);
    unsigned int origin_result_num = tmp_results.size();

    get_correct_py_fuzzy(tmp_cmd, tmp_results, type);
    get_correct_py_add_ed(tmp_cmd.query, tmp_results, type);
    get_correct_py_miss_ed(tmp_cmd.query, tmp_results, type);
    get_correct_py_reverse_ed(tmp_cmd.query, tmp_results, type);
    get_correct_py_add_ed(tmp_cmd.query, tmp_results, type);
    get_correct_py_wrong_ed(tmp_cmd.query, tmp_results, type);
    unsigned int ret_num = tmp_results.size();


    print_time( old_tv, "get_py2query", cmd.query );
    int im_flag = 0;
    if (tmp_results.size() == 0)
    {
        im_flag = 1;
        get_correct_im( (char*)(py.c_str() ), tmp_results,  PY_SEGMENT_TOTAL_IM);
        ret_num = tmp_results.size() ;
        if  (ret_num == 0)
            return 0;
    }

   
    for (unsigned int i=0; i<ret_num; i++)
    {
        QY_TIME;
        string tmp;
        tmp += pre;
        tmp += string( tmp_results[i].query );
        tmp += post;

        double tmp_lm = mlang_get_flogp_sentence(mlang, tmp.c_str(), LM_N_GRAM);

        if (i >= origin_result_num)
            tmp_results[i].lm = tmp_lm * 1.1;
        else
            tmp_results[i].lm = tmp_lm;

        unsigned int len = tmp.length();
        if (len == strlen(cmd.query) )
        {
            if ( 0 == strcmp( cmd.query, tmp.c_str() ) )
            {
                tmp_results[i].lm = 99999;
            }
        }

        if (freq != 0)
        {
            uint64 segment_id =  first_half_md5(tmp_results[i].query, strlen(tmp_results[i].query ) )  ;
            uint32 segment_freq = get_freq(segment_id);

            if (freq >=  segment_freq && freq > 500)
                tmp_results[i].lm = 99999;
        }

        strncpy(tmp_results[i].query, tmp.c_str(), len );
        (tmp_results[i].query)[len] = '\0'; 
        print_time( old_tv, "stringn and mlang_get_flogp_sentence", tmp_results[i].query);
    }
    stable_sort (tmp_results.begin(), tmp_results.end(), results_sort_by_lm);

    // bug is :
    // query=安全卫士8.8离线包 b9a32bba7fecac68    3   0   7   0   80.764955
    // 
    double threshold =  LM_THRESHOLD;
    if (type1.number == 1)
        threshold += 50;

    // 百度一下文库东西怎么没有了, lm = 63
    int source_query_len = strlen(tmp_results[0].query);
    if( source_query_len > 20 )
        threshold += 30;
    else if (source_query_len > 15)
        threshold += 15;

    if ( (tmp_results[0]).lm < threshold ) 
    {
        results.push_back( tmp_results[0] );
    }

    return 0;
}







int get_correct_cn_miss_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    if (strlen(query) <= 4)   return 0;

    DataWarehouse * dwh = g_dwh;

    int result_number = 0;
    uint64 id = first_half_md5(query, strlen(query));
    result_number = dwh->get_cned2query(id, results, type);

    build_result_freq_cooc(query, results);
    
    for (unsigned int i=0; i<results.size(); i++)
    {
        double result_lm = mlang_get_flogp_sentence(mlang, results[i].query, LM_N_GRAM);
        results[i].lm    = result_lm;
    }
    return 0;

}

int get_correct_cn_reverse_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    if (strlen(query) <= 4)   return 0;

    vector<string>  w1_list;
    split_query(query, w1_list);
    

    for(unsigned int i=0; i<w1_list.size() -1; i++ )
    {
        char c1 = w1_list[i][0];
        char c2 = w1_list[i+1][0];

        if ( char_is_in_cn(c1) && char_is_in_cn(c2) )
        {
            correct_result_t one_result;

            for (unsigned int j=0; j<w1_list.size(); j++ )
            {
                if (j == i)
                {
                    strcat(one_result.query, w1_list[i+1].c_str() );
                }
                else if (j == i+1)
                {
                    strcat(one_result.query, w1_list[i].c_str() );
                }
                else
                {
                    strcat(one_result.query, w1_list[j].c_str() );
                }
            }
            double result_lm = mlang_get_flogp_sentence(mlang, one_result.query, LM_N_GRAM);
            one_result.lm    = result_lm;
            one_result.type  = type;
            results.push_back(one_result);
        }
    }

    build_result_freq_cooc(query, results);

    return 0;
}



int get_correct_cn_wrong_ed(char* query, vector<correct_result_t> &results, result_type type)
{
    if (strlen(query) <= 4)   return 0;

    DataWarehouse * dwh = g_dwh;
    int result_number = 0;

    vector<string>  w1_list;
    split_query(query, w1_list);

    vector<string>  w2_list;

    vector<correct_result_t> tmp_results;

    for(unsigned int i=0; i<w1_list.size(); i++ )
    {
        char c1 = w1_list[i][0];

        if ( char_is_in_cn(c1) )
        {
            char buf[64];
            memset(buf , 0, 64);

            for (unsigned int j=0; j<w1_list.size(); j++ )
            {
                if (j != i)
                {
                    strcat(buf, w1_list[j].c_str() );
                }
            }

            tmp_results.clear();

            uint64 id = first_half_md5(buf, strlen(buf));
            result_number += dwh->get_cned2query(id, tmp_results, type);

            for(unsigned int j=0; j<tmp_results.size(); j++)
            {
                w2_list.clear();
                split_query(tmp_results[j].query, w2_list);
                if (w2_list[i] != w1_list[i] && word_is_similar (w2_list[i], w1_list[i]) == 1)
                    results.push_back(tmp_results[j] );
            }
            
        }
    }

    build_result_freq_cooc(query, results);
    
    for (unsigned int i=0; i<results.size(); i++)
    {
        double result_lm = mlang_get_flogp_sentence(mlang, results[i].query, LM_N_GRAM);
        results[i].lm    = result_lm;
    }
    return 0;

}


//uery=smtupian 26188178a12eaec5    30  4   1   2   both en char, cooc > 0
int py_control_result( request & cmd, vector<correct_result_t> &results)
{
    int rarequery_flag = 0;

    if (cmd.title + cmd.abs + cmd.url == 0)
        rarequery_flag = 1;

    uint32 len1 = strlen(cmd.query);
    uint64 queryid =  first_half_md5(cmd.query, len1) ;
    uint32 freq = get_freq(queryid);
    
    vector<string>  w1_list;
    split_query(cmd.query, w1_list);

    query_type type1;
    parse_query(cmd.query, type1);

    uint32 score = 0 ;

    for (uint32 i =0 ; i < results.size(); i++)
    {
        if (results[i].type != PY_TOTAL &&  results[i].type!= PY_FUZZY)
            continue;

        if (score >= 5)
            continue;
       
        uint32 len2 = strlen(results[i].query );

        if (len2 <= 3)
        {
            build_result_confidence( results[i], 0, "to shoort result query");
            continue;
        }  

        if (len1 == len2 && !strcasecmp(cmd.query, results[i].query)   )
        {
            build_result_confidence( results[i], 0, "dup");
            continue;
        }   

        uint32 reverse_cooc = get_cooc(results[i].query, cmd.query);

        float cooc_flag = float(results[i].freq + 1) * float(results[i].cooc + 1) / float(freq + 1) / float(reverse_cooc + 1);

        if (results[i].cooc == 0 && reverse_cooc == 0 && freq == 0 )
            cooc_flag = 0.8888;

        if (debug>=2) printf("freq %d reverse_cooc %d cooc_falg %f \n", freq, reverse_cooc, cooc_flag);

        query_type type2;
        parse_query(results[i].query, type2);


        //printf("type1-----%d %d %d %d %d\n", type1.cn, type1.en, type1.puncation, type1.blank, type1.number);
        //printf("type2-----%d %d %d %d %d\n", type2.cn, type2.en, type2.puncation, type2.blank, type2.number);
        if (  is_pure_cn_query(type1) && is_pure_cn_query(type2) )
        {
            //printf("hit pure cn query \n");
            if ( 1 == filter_by_lm(cmd, results[i])   )
            {
                //printf("hit pure cn query ! filter\n");
                char tmp_buf[64];
                memset(tmp_buf, 0, 64);
                sprintf(tmp_buf, "filter by lm: %lf %lf", cmd.lm, results[i].lm);
                build_result_confidence( results[i], 0, tmp_buf);
                continue;
            }
        }

        if (type1.cn==1 && type1.en == 0 && type2.cn==1 && type2.en==0 && len1 == 4  )
        {
            build_result_confidence( results[i], 0, "both two cn-word, filter it");
            continue;
        }

        vector<string>  w2_list;
        split_query(results[i].query, w2_list);

        if ( 1 == diff_in_location(w1_list, w2_list) )
        {
            build_result_confidence( results[i], 0, "diff in location");
            continue;
        }


        vector<string> different;
        uint32 same_word = cn_query_similar(w1_list, w2_list, different);
        if (type1.cn == 1 && type2.cn == 1 && same_word == 0)
        {
            build_result_confidence( results[i], 0, "same word is 0");
            continue;
        }

        if (different.size() ==1 &&  diff_in_stopword(different) )
        {
            build_result_confidence( results[i], 0, "diff in stop word");
            continue;
        }


        uint32 difftype = diff_type(different);


        if (debug>=2) printf("result query %s\n", results[i].query);
        if (type1.cn == 0 )
        {
            if (type2.cn == 0)
            {
                if ( (int)different.size() == 1 && difftype >= 4  ) //MARK
                    build_result_confidence( results[i], 0, "type error");
                else if (len1 < 4 && len2 <= 4)
                    build_result_confidence( results[i], 0, "en char too short");
                else if ( freq > 100* results[i].freq)
                    build_result_confidence( results[i], 0, "freq > 100* result freq");
                else if ( results[i].cooc > 100)
                {
                    build_result_confidence( results[i], 3, "both en char, cooc > 100");
                    score += 3;
                }
                else if ( results[i].cooc > 0)
                {
                    build_result_confidence( results[i], 2, "both en char, cooc > 0");
                    score += 2;
                }
                else if ( results[i].freq > 100)
                {
                    build_result_confidence( results[i], 1, "both en char, freq > 100"); score += 1;
                }
                else if ( results[i].freq > 3){
                    build_result_confidence( results[i], 1, "both en char, cooc > 0"); score += 1;
                }
                else
                    build_result_confidence( results[i], 0, "both en char, else");
                if (debug>=2) printf("11result query %s\n", results[i].query);
            }
            else
            {
                if (freq == 0)
                {
                    if (results[i].cooc > 5 || results[i].freq > 50) 
                    {
                        build_result_confidence( results[i], 3, "en, cn , freq0, cooc > 5, freq > 50");
                        score += 3;
                    }
                    else if (results[i].freq > 10)
                    {
                        build_result_confidence( results[i], 2, "en, cn , freq0,  freq > 10");
                        score += 2;
                    }
                    else if (results[i].freq > 0)
                    {
                        build_result_confidence( results[i], 1, "en, cn , freq0, freq > 0");
                        score += 1;
                    }
                    else
                        build_result_confidence( results[i], 0, "en, cn , freq0, else");
                }
                else
                {
                    if ( freq > 100* results[i].freq)
                        build_result_confidence( results[i], 0, "en, cn , freq !0, freq > 100 result freq");
                    else if (results[i].cooc > 10 && freq < results[i].freq  )
                    {
                        build_result_confidence( results[i], 3, "en, cn , freq!0, cooc > 10");
                        score += 3;
                    }
                    else if (results[i].cooc > 0)
                    {
                        build_result_confidence( results[i], 2, "en, cn , freq0,  freq > 10");
                        score += 2;
                    }
                    else if (results[i].freq > 3)
                    {
                        build_result_confidence( results[i], 1, "en, cn , freq0, freq > 0");
                        score += 1;
                    }
                    else
                        build_result_confidence( results[i], 0, "en, cn , freq0, else");
                }
                if (debug>=2) printf("3333result query %s\n", results[i].query);
            }
        }
        else // type1.cn == 1
        {
            //  source query = 金三jian
            //   query=金三角 bf5af5e43e56a268    12967   0   6   0   16.414529   
            //   99999.000000    394 cn, cn,len1 == 8, query coverage < 0.65
            float query_coverage = 0.1;
            if (type1.en == 0 && type1.number == 0  )
                query_coverage = float(  same_word) / float(max(w1_list.size(), w2_list.size()  ) );
            else
                query_coverage = float(  same_word) / float(min(w1_list.size(), w2_list.size()  ) );

            //printf("query coverage %s %f\n", results[i].query,  query_coverage);
            if (debug>=2) printf("query coverage %f\n", query_coverage);

            if (type2.cn == 0)
            {
                build_result_confidence( results[i], 0, "cn, en,pass");
                if (debug>=2) printf("444result query %s\n", results[i].query);
            }
            else if(type1.en==0 && type2.en == 1)
            {
                build_result_confidence( results[i], 0, "cn, cn+en,pass");
            }
            else  // type2.cn = 1 
            {
                if (len1 <= 10 && type1.cn == 1 && type1.en == 0 )
                {
                    if (freq >= 9  && cooc_flag < 2)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, freq >=9 cooc flag < 2");
                        continue;
                    }

                    if (results[i].freq <= 3)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, result freq  <= 3");
                        continue;
                    }

                    if (freq >= 100  && cooc_flag < 5)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, freq >=100 cooc flag < 5");
                        continue;
                    }

                }

                if (len1 == 4)
                {
                    if (freq >= 500 )
                        build_result_confidence( results[i], 0, "cn, cn, len1 == 4, 2 word freq >=500");
                    else if (len2 == 4 && same_word < 1 && type2.cn == 1)
                        build_result_confidence( results[i], 0, "cn, cn, len1 == 4,similar < 1");
                    else if (freq > 2* results[i].freq )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 4,freq > 2*result freq");
                    else
                    {
                        if (results[i].cooc > 200 && results[i].freq > 1000)
                        {
                            build_result_confidence( results[i], 3, "cn, cn,len1 == 4,freq > 1000  cooc > 200");
                            score += 3;
                        }
                        else if (results[i].cooc > 10 && results[i].freq > 100)
                        {
                            build_result_confidence( results[i], 2, "cn, cn,len1 == 4,freq > 100 cooc > 10");
                            score += 2;
                        }
                        else if (results[i].cooc > 2 && results[i].freq > 10)
                        {
                            build_result_confidence( results[i], 1, "cn, cn,len1 == 4,freq > 100 cooc > 10");
                            score += 1;
                        }
                        else
                        {
                            if (freq == 0 && results[i].freq > 1 )
                            {
                                if (rarequery_flag == 1 )
                                {
                                    build_result_confidence( results[i], 2, "cn, cn,len1 == 4,freq = 0 rearflag == 1");
                                    score += 2;
                                }
                                else
                                {
                                    build_result_confidence( results[i], 1, "cn, cn,len1 == 4,freq = 0 rearflag == 0");
                                    score += 1;
                                }
                            }
                            else
                                build_result_confidence( results[i], 0, "cn, cn,len1 == 4,freq > 0, else");
                        }
                    }
                    if (debug>=2) printf("5555result query %s\n", results[i].query);
                }
                else if (len1 == 6)
                {
                    if (len1 != len2 && len2 < 7 && type1.en == 0)
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, freq > 0, len1!=len2");
                    else if (same_word < 2 && type1.en == 0)
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, same word < 2");
                    else if (type2.en == 1 && type1.en == 0) //MARK
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, type2 en == 1");
                    else if (query_coverage < 0.65 )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, query coverage < 0.65");
                    else if (freq > results[i].freq * 2 )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, freq > 2 * result freq");
                    else
                    {
                        if (results[i].cooc > 200 && results[i].freq > 1000)
                        {
                            build_result_confidence( results[i], 3, "cn, cn,len1 == 6,freq > 1000  cooc > 200");
                            score += 3;
                        }
                        else if (results[i].cooc > 10 && results[i].freq > 100)
                        {
                            build_result_confidence( results[i], 2, "cn, cn,len1 == 6,freq > 100 cooc > 10");
                            score += 2;
                        }
                        else if (results[i].cooc > 0 && results[i].freq > 3)
                        {
                            build_result_confidence( results[i], 1, "cn, cn,len1 == 6,freq > 3 cooc > 0");
                            score += 1;
                        }
                        else
                        {
                            if (freq == 0 && results[i].freq > 1 )
                            {
                                if (rarequery_flag == 1 )
                                {
                                    build_result_confidence( results[i], 2, "cn, cn,len1 == 6,freq = 0 rearflag == 1");
                                    score += 2;
                                }
                                else
                                {
                                    build_result_confidence( results[i], 1, "cn, cn,len1 == 6,freq = 0 rearflag == 0");
                                    score += 1;
                                }
                            }
                            else
                                build_result_confidence( results[i], 0, "cn, cn,len1 == 6,freq > 0, else");
                        }                        
                    }
                    if (debug>=2) printf("666 result query %s\n", results[i].query);
                }

                else if (len1 == 8)
                {
                    if (type2.en == 1 && type1.en == 0)
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 8, result type en==1");
                    else if (len2 == 8 && same_word < 3 && type1.en == 0)
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 8, same word < 2");
                    else if (query_coverage < 0.65 )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 8, query coverage < 0.65");
                    else if (freq > results[i].freq * 2 )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 8, freq > 2 * result freq");
                    else if (results[i].cooc > 200 && results[i].freq > 1000)
                    {
                        build_result_confidence( results[i], 3, "cn, cn,len1 == 8,freq > 1000  cooc > 200");
                        score += 3;
                    }
                    else if (results[i].cooc > 5 )
                    {
                        build_result_confidence( results[i], 2, "cn, cn,len1 == 8, cooc > 5");
                        score += 2;
                    }
                    else if (results[i].cooc > 0 )
                    {
                        build_result_confidence( results[i], 1, "cn, cn,len1 == 8, cooc > 0");
                        score += 1;
                    }
                    else if (freq == 0 && results[i].freq > 1 )
                    {
                        if (rarequery_flag == 1 )
                            build_result_confidence( results[i], 2, "cn, cn,len1 == 8,freq = 0 rearflag == 1");
                        else
                            build_result_confidence( results[i], 1, "cn, cn,len1 == 8,freq = 0 rearflag == 0");
                        score += 1; //lazy
                    }
                    else
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 8,freq > 0, else");
                    if (debug>=2) printf("777 result query %s\n", results[i].query);
                }
                else
                {
                    if (type2.en == 0  && query_coverage < 0.79 && type1.en == 0)
                        build_result_confidence( results[i], 0, "cn, cn,else, result type cn=1 coverage < 0.79");

                    else if (different.size() ==1 &&  diff_in_cnnumber(different) )
                        build_result_confidence( results[i], 0, "cn, cn,else,  diff in cn numbers ");
                    else if (freq > results[i].freq * 2 && results[i].cooc < 100 )
                        build_result_confidence( results[i], 0, "cn, cn,else,  freq > 2 * result freq and cooc < 100");
                    else if (cooc_flag < 0.75 )
                        build_result_confidence( results[i], 0, "cn, cn,else, cooc_flag < 0.75 ");
                    else if (freq == 0)
                    {
                        if (results[i].cooc > 5 || results[i].freq > 50)
                        {
                            build_result_confidence( results[i], 3, "cn, cn,else,freq =0, cooc>5 freq>50");
                            score += 3;
                        }
                        else if (results[i].freq > 10 || results[i].cooc > 1 )
                        {
                            build_result_confidence( results[i], 2, "cn, cn,else,freq =0, cooc > 5");
                            score += 2;
                        }
                        else if ( results[i].cooc > 0 || results[i].freq > 1)
                        {
                            build_result_confidence( results[i], 1, "cn, cn,else,freq =0, cooc > 0");
                            score += 1;
                        }
                        else
                            build_result_confidence( results[i], 0, "cn, cn,else,freq =0, else");
                    }
                    else
                    {
                        if (results[i].cooc > 10)
                        {
                            build_result_confidence( results[i], 3, "cn, cn,else,freq > 0,  cooc > 10"); score += 3;
                        }
                        else if (results[i].cooc > 0 )
                        {
                            build_result_confidence( results[i], 2, "cn, cn,else,freq > 0, cooc > 0"); score += 2;
                        }
                        else if (results[i].freq > 3)
                        {
                            build_result_confidence( results[i], 1, "cn, cn,else,freq > 0, cooc > 0"); score += 1;
                        }
                        else
                            build_result_confidence( results[i], 0, "cn, cn,len1 == 8,freq > 0, else"); 
                    } 
                    if (debug>=2) printf(" 888 result query %s\n", results[i].query);                  
                }
            }
        }

        if (debug>=2) printf("size %d %d\n", (int)w2_list.size(), (int)different.size());
        w2_list.clear();
        different.clear();
    }
    w1_list.clear();
    return 0;
}



int py_ed_control_result( request & cmd, vector<correct_result_t> &results)
{
    int rarequery_flag = 0;

    if (cmd.title + cmd.abs + cmd.url == 0)
        rarequery_flag = 1;

    int len1 = strlen(cmd.query);
    uint64 queryid =  first_half_md5(cmd.query, len1) ;
    uint32 freq = get_freq(queryid);

    query_type type1;
    parse_query(cmd.query, type1);


    char new_query[64];    
    if (freq == 0 && type1.cn == 1 && type1.en == 0)
    {
        int blank_number = strip_blank(cmd.query, new_query,len1);
        if (blank_number > 0)
        {
            len1 = strlen(new_query); //MARK
            queryid =  first_half_md5(new_query, len1) ;
            freq = get_freq(queryid);
        }
        else
        {
            strcpy(new_query, cmd.query);
        }
    }
    else
    {
        strcpy(new_query, cmd.query);
    }

    vector<string>  w1_list;
    split_query(new_query, w1_list);  


    for (uint32 i =0 ; i < results.size(); i++)
    {
        if ( !(results[i].type == PY_ED_MISS  || results[i].type == PY_ED_ADD || results[i].type == PY_ED_REVERSE ) )
            continue;


        int len2 = strlen(results[i].query );
        if (len2 <=3 )
        {
            build_result_confidence( results[i], 0, "to shoort result query");
            continue;
        }  

        if (len1 == len2 && !strcasecmp(new_query, results[i].query)   )
        {
            build_result_confidence( results[i], 0, "dup");
            continue;
        }   

        uint32 reverse_cooc = get_cooc(results[i].query, new_query);

        float cooc_flag = float ( (results[i].freq + 1) * (results[i].cooc + 1) )/ float(freq + 1) / float(reverse_cooc + 1);

        //if (results[i].cooc == 0 && reverse_cooc == 0 && freq == 0 )
        //    cooc_flag = 0.8888;

        if (debug>=2) printf("freq %d reverse_cooc %d cooc_falg %f \n", freq, reverse_cooc, cooc_flag);

        query_type type2;
        parse_query(results[i].query, type2);

        if (  is_pure_cn_query(type1) && is_pure_cn_query(type2) )
        {
            if ( 1 == filter_by_lm(cmd, results[i])   )
            {
                char tmp_buf[64];
                memset(tmp_buf, 0, 64);
                sprintf(tmp_buf, "filter by lm: %lf %lf", cmd.lm, results[i].lm);
                build_result_confidence( results[i], 0, tmp_buf);
            }
        }

        if (type1.cn==1 && type1.en == 0 && type2.cn==1 && type2.en==0 && len1 == 4 )
        {
            build_result_confidence( results[i], 0, "both two cn-word, filter it");
            continue;
        }

        vector<string>  w2_list;
        split_query(results[i].query, w2_list);

        if ( 1 == diff_in_location(w1_list, w2_list) )
        {
            build_result_confidence( results[i], 0, "diff in location");
            continue;
        }

        vector<string> different;
        uint32 same_word = cn_query_similar(w1_list, w2_list, different);
        if (type1.cn == 1 && type2.cn == 1 && same_word == 0)
        {
            build_result_confidence( results[i], 0, "same word is 0");
            continue;
        }

        uint32 difftype = diff_type(different);
        
        // 漫画h   漫画, 这种只差一个字母的, 不过滤
        if ( different.size() == 1  &&  difftype == 1 &&  results[i].type == PY_ED_MISS )
        {

            // 黄s片 should not filter
            if ( w1_list[0].length() ==1 || w1_list[ w1_list.size()-1 ].length() == 1 )
            {
                build_result_confidence( results[i], 0, "diff in only one en char");
                continue;  
            }          
        }

        if (different.size() ==1 &&  diff_in_stopword(different) )
        {
            build_result_confidence( results[i], 0, "diff in stop word");
            continue;
        }

        if (difftype == 8)
        {
            build_result_confidence( results[i], 0, "diff in punc");
            continue;
        }

        if (debug>=2)  printf("result query %s\n", results[i].query);
        if (type1.cn == 0 )
        {
            if (type2.number == 1 && type2.en==0 && type2.cn ==0)
            {
                build_result_confidence( results[i], 0, "type 2 in pure number");
                continue;
            }
            else if (type2.cn == 0 && type2.en == 1)
            {
                if ( results[i].cooc > 10 && cooc_flag > 5 )
                {
                    build_result_confidence( results[i], 2, "both en char, cooc > 10");
                    continue;
                }

                if (results[i].cooc > 0 && cooc_flag > 10)
                {
                    build_result_confidence( results[i], 1, "both en char, cooc > 0");
                    continue;
                }
                else
                {
                    build_result_confidence( results[i], 0, "both en char, else");
                }
            }
            else if (type2.cn == 1 && type2.en == 1)
            {
                build_result_confidence( results[i], 0, "en query, cn+en result");
            }
            else 
            {
                if (type1.en ==1 && type1.number == 1)  //err__3aaa__啊啊啊__-_
                    build_result_confidence( results[i], 0, "en query, cn+en result");
                else if (freq == 0)
                {
                    if ( results[i].cooc > 5 || results[i].freq > 50)
                        build_result_confidence( results[i], 3, "en,cn , freq =0, cooc > 5 or result freq > 50");   
                    else if (results[i].freq > 10 )
                        build_result_confidence( results[i], 2, "en,cn, freq=0, result freq > 10"); 
                    else if (results[i].freq >  0)
                        build_result_confidence( results[i], 1, "en,cn , freq =0, result freq > 0");
                    else 
                        build_result_confidence( results[i], 0, "en,cn , freq =0, else");        
                }
                else
                {
                    if ( freq > 100* results[i].freq)
                        build_result_confidence( results[i], 0, "en cn, freq > 0, freq>100* result freq");
                    else if (cooc_flag < 0.5 )
                        build_result_confidence( results[i], 0, "en,cn , freq>0, cooc_flag < 0,5 ");
                    else if  (results[i].cooc > 10)
                        build_result_confidence( results[i], 3, "en,cn , freq>0, cooc > 10 ");
                    else if (results[i].cooc > 0)
                        build_result_confidence( results[i], 2, "en,cn , freq>0, cooc > 0");
                    else if (results[i].freq > 3)
                        build_result_confidence( results[i], 1, "en,cn , freq>0, freq > 3 ");
                    else
                        build_result_confidence( results[i], 0, "en,cn , freq>0, else ");
                }

            }
        }
        else
        {
            //float query_coverage = float(  same_word )/ float( max(w1_list.size(), w2_list.size()  ) );
            float query_coverage = 0.1;
            if (type1.en == 0 && type1.number == 0  )
                query_coverage = float(  same_word) / float(max(w1_list.size(), w2_list.size()  ) );
            else
                query_coverage = float(  same_word) / float(min(w1_list.size(), w2_list.size()  ) );

            if (debug>=2) printf("query coverage %f\n", query_coverage);

            if (type2.cn == 0 && type2.en == 1 && type1.cn == 1)
            {
                build_result_confidence( results[i], 0, "cn, en,pass");
                continue;
            }

            if (type2.cn == 1 && type2.en == 1 && type1.cn == 1 && type1.en == 1)
            {
                if (results[i].cooc < 2)
                {
                    build_result_confidence( results[i], 0, "both type 1+2");
                    continue;
                }

                //有一些iphone4充电---iphone4s充电的case, add策略不支持都是类型3的纠错
                if (results[i].type == PY_ED_ADD && results[i].cooc == 0)
                {
                    build_result_confidence( results[i], 0, "py_ed_add, for type 1+2");
                    continue;
                }
            }

            if(type1.en==0 && type2.en == 1 && type2.cn ==1 && type1.cn == 1)
            {
                build_result_confidence( results[i], 0, "cn, cn+en,pass");
                continue;
            }

            if (type2.cn == 1 && type2.en == 0 && type1.cn == 1 && type1.en == 0)
            {
                if (abs(  int(len1 - len2)  ) == 1)
                {
                    build_result_confidence( results[i], 0, "only different in number or punc");
                    continue;
                }
            }

            if (len1 <= 14)
            {
                if (results[i].freq >= 9 && cooc_flag < 2 )
                {
                    build_result_confidence( results[i], 0, "cn, cn, len1<14, freq>9 and cooc_flag < 2");
                    continue;
                }

                if (results[i].freq <= 5)
                {
                    build_result_confidence( results[i], 0, "cn, cn, len1<14, freq < 5");
                    continue;
                }

                if (results[i].freq >=20 && cooc_flag < 5)
                {
                    build_result_confidence( results[i], 0, "cn, cn, len1<14, freq > 20 cooc_flag < 5");
                    continue;
                }                
            }            

            if (len1 == 4)
            {
                if (len2 == 4 && same_word < 1 )
                    build_result_confidence( results[i], 0, "cn, cn, len1 == 4,similar < 1");
                else if (freq > 2* results[i].freq )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 4,freq > 2*result freq");
                else if (freq >= 500 )
                    build_result_confidence( results[i], 0, "cn, cn, len1 == 4, 2 word freq >=500");
                else if (results[i].cooc > 200 && results[i].freq > 1000)
                    build_result_confidence( results[i], 3, "cn, cn,len1 == 4,freq > 1000  cooc > 200");
                else if (results[i].cooc > 10 && results[i].freq > 100)
                    build_result_confidence( results[i], 2, "cn, cn,len1 == 4,freq > 100 cooc > 10");
                else if (results[i].cooc > 2 && results[i].freq > 10)
                    build_result_confidence( results[i], 1, "cn, cn,len1 == 4,freq > 100 cooc > 10");
                else
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 4,else");

            }

            if (len1 == 6)
            {
                if (len1 != len2 && len2 < 7)
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 6, freq > 0, len1!=len2");
                else if (same_word < 2 )
                        build_result_confidence( results[i], 0, "cn, cn,len1 == 6, same word < 2");
                else if (type2.en == 1 ) //MARK
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 6, type2 en == 1");
                else if (query_coverage < 0.65 )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 6, query coverage < 0.65");
                else if (freq > results[i].freq * 2 )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 6, freq > 2 * result freq");
                else if (results[i].cooc > 200 && results[i].freq > 1000)
                    build_result_confidence( results[i], 3, "cn, cn,len1 == 6,freq > 1000  cooc > 200");
                else if (results[i].cooc > 10 && results[i].freq > 100)
                    build_result_confidence( results[i], 2, "cn, cn,len1 == 6,freq > 100 cooc > 10");
                else if (results[i].cooc > 0 && results[i].freq > 3)
                    build_result_confidence( results[i], 1, "cn, cn,len1 == 6,freq > 3 cooc > 0");
                else
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 6,else");
            }

            if (len1 == 8)
            {
                if (type2.en == 1)
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 8, result type en==1");
                else if (len2 == 8 && same_word < 3 )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 8, same word < 2");
                else if (query_coverage < 0.65 )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 8, query coverage < 0.74");
                else if (freq > results[i].freq * 2 )
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 8, freq > 2 * result freq");
                else if (results[i].cooc > 200 && results[i].freq > 1000)
                    build_result_confidence( results[i], 3, "cn, cn,len1 == 8,freq > 1000  cooc > 200");
                else if (results[i].cooc > 5 )
                    build_result_confidence( results[i], 2, "cn, cn,len1 == 8, cooc > 5");
                else if (results[i].cooc > 0 && results[i].freq > 5)
                    build_result_confidence( results[i], 1, "cn, cn,len1 == 8, cooc > 0 andf result freq > 5");
                else if (results[i].freq > 10000)
                    build_result_confidence( results[i], 1, "cn, cn,len1 == 8, result freq > 10000");
                else
                    build_result_confidence( results[i], 0, "cn, cn,len1 == 8,else");
            }

            else
            {
                //if (type2.en == 0  && query_coverage < 0.79 && rarequery_flag == 0)
                if (type2.en == 0  && query_coverage < 0.79) // && rarequery_flag == 0)
                    build_result_confidence( results[i], 0, "cn, cn,else, result type cn=1 coverage < 0.79");

                else if (different.size() ==1 &&  ( diff_in_cnnumber(different) || diff_in_number(different) )  )
                    build_result_confidence( results[i], 0, "cn, cn,else,  diff in cn numbers ");

                else if (freq == 0 )
                {
                    if (results[i].cooc > 5 || results[i].freq > 50)
                        build_result_confidence( results[i], 3, "cn, cn,else,freq =0, cooc>5 freq>50");
                    else if (results[i].freq > 10  )
                        build_result_confidence( results[i], 2, "cn, cn,else,freq =0, cooc > 5");
                    else if ( results[i].freq > 1)
                        build_result_confidence( results[i], 1, "cn, cn,else,freq =0, cooc > 0");
                    else
                        build_result_confidence( results[i], 0, "cn, cn,else,freq =0, else");
                }
                else
                {
                    if ( (freq > results[i].freq * 2) && results[i].cooc < 100 )
                        build_result_confidence( results[i], 0, "cn, cn, else,freq > 0, freq>2*resuolt.freq");
                    else if (cooc_flag < 0.75)
                        build_result_confidence( results[i], 0, "cn, cn, else,freq > 0, cooc flag < 0,75 ");
                    else if (results[i].cooc > 10)
                        build_result_confidence( results[i], 3, "cn, cn,else,freq > 0,  cooc > 10");
                    else if (results[i].cooc > 0 )
                        build_result_confidence( results[i], 2, "cn, cn,else,freq > 0, cooc > 0");
                    else if (results[i].freq > 3)
                        build_result_confidence( results[i], 1, "cn, cn,else,freq > 0, cooc > 0");
                    else 
                        build_result_confidence( results[i], 0, "cn, cn,else,freq > 0, else"); 
                }





                if (len1 <= 10 && type1.cn == 1 && type1.en == 0 )
                {
                    if (freq >= 9  && cooc_flag < 2)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, freq >=9 cooc flag < 2");
                        continue;
                    }

                    if (results[i].freq <= 3)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, result freq  <= 3");
                        continue;
                    }

                    if (freq >= 100  && cooc_flag < 5)
                    {
                        build_result_confidence( results[i], 0, "cn, cn, freq >=100 cooc flag < 5");
                        continue;
                    }

                }


            }
        }

        if (debug>=2) printf("size %d %d\n", (int)w2_list.size(), (int)different.size());
        w2_list.clear();
        different.clear();
    }
    w1_list.clear();
    return 0;
}



int py_wrong_ed_control_result( request & cmd, vector<correct_result_t> &results)
{
    int rarequery_flag = 0;

    if (cmd.title + cmd.abs + cmd.url == 0)
        rarequery_flag = 1;

    int len1 = strlen(cmd.query);
    uint64 queryid =  first_half_md5(cmd.query, len1) ;
    uint32 freq = get_freq(queryid);

    query_type type1;
    parse_query(cmd.query, type1);


    char new_query[64];    
    if (freq == 0 && type1.cn == 1 && type1.en == 0)
    {
        int blank_number = strip_blank(cmd.query, new_query,len1);
        if (blank_number > 0)
        {
            len1 = strlen(new_query); //MARK
            queryid =  first_half_md5(new_query, len1) ;
            freq = get_freq(queryid);
        }
        else
        {
            strcpy(new_query, cmd.query);
        }
    }
    else
    {
        strcpy(new_query, cmd.query);
    }

    vector<string>  w1_list;
    split_query(new_query, w1_list);  


    for (uint32 i =0 ; i < results.size(); i++)
    {
        if (results[i].type != PY_ED_WRONG)
            continue;

        if (freq>50 )
        {
            build_result_confidence( results[i], 0, "source query freq  50");
            continue;
        } 

        // 对于包含中文的query严格一些, 纯英文串则应该松一些
        if (freq>0 && results[i].cooc < 5 && type1.cn == 1)
        {
            build_result_confidence( results[i], 0, "stick rules for py_wrong ed");
            continue;
        } 

        int len2 = strlen(results[i].query );
        if (len2 <=3 )
        {
            build_result_confidence( results[i], 0, "to shoort result query");
            continue;
        }  

        if (len1 == len2 && !strcasecmp(new_query, results[i].query)   )
        {
            build_result_confidence( results[i], 0, "dup");
            continue;
        }   

        uint32 reverse_cooc = get_cooc(results[i].query, new_query);
        float cooc_flag = float (  (results[i].freq + 1) * (results[i].cooc + 1)) / float( (freq + 1) ) / float(reverse_cooc + 1) ;

        //if (results[i].cooc == 0 && reverse_cooc == 0 && freq == 0 )
        //    cooc_flag = 0.8888;

        if (debug>=2) printf("freq %d reverse_cooc %d cooc_falg %f \n", freq, reverse_cooc, cooc_flag);

        if (cooc_flag < 2)
        {
            build_result_confidence( results[i], 0, "cooc_flag  < 2");
            continue;
        }   

        query_type type2;
        parse_query(results[i].query, type2);

        if (  is_pure_cn_query(type1) && is_pure_cn_query(type2) )
        {
            if ( 1 == filter_by_lm(cmd, results[i])   )
            {
                char tmp_buf[64];
                memset(tmp_buf, 0, 64);
                sprintf(tmp_buf, "filter by lm: %lf %lf", cmd.lm, results[i].lm);
                build_result_confidence( results[i], 0, tmp_buf);
            }
        }

        if (type1.cn==1 && type1.en == 0 && type2.cn==1 && type2.en==0 && len1 == 4  )
        {
            build_result_confidence( results[i], 0, "both two cn-word, filter it");
            continue;
        }

        vector<string>  w2_list;
        split_query(results[i].query, w2_list);

        if ( 1 == diff_in_location(w1_list, w2_list) )
        {
            build_result_confidence( results[i], 0, "diff in location");
            continue;
        }


        vector<string> different;
        uint32 same_word = cn_query_similar(w1_list, w2_list, different);
        if (type1.cn == 1 && type2.cn == 1 && same_word == 0)
        {
            build_result_confidence( results[i], 0, "same word is 0");
            continue;
        }

        uint32 difftype = diff_type(different);

        //float query_coverage = float(  same_word) /float( max(w1_list.size(), w2_list.size()  ) );
        float query_coverage = 0.1;
        if (type1.en == 0 && type1.number == 0  )
            query_coverage = float(  same_word) / float(max(w1_list.size(), w2_list.size()  ) );
        else
            query_coverage = float(  same_word) / float(min(w1_list.size(), w2_list.size()  ) );

        if (debug>=2) printf("query coverage %f\n", query_coverage);

        if (difftype == 8 || difftype == 4)
        {
            build_result_confidence( results[i], 0, "diff in punc or number");
            continue;
        }


        if (len1<=10 && type1.cn==1 && type1.en == 0)
        {
            if (freq >= 9 && cooc_flag < 2)
            {
                build_result_confidence( results[i], 0, "freq>9  cooc_flag < 2");
                continue;
            }

            if (results[i].freq <=5 )
            {
                build_result_confidence( results[i], 0, "result freq <= 5");
                continue;
            }
        }

        if (diff_in_cnnumber(different))
        {
            build_result_confidence( results[i], 0, "diff in cnnumber");
            continue;
        }


        ED ed;
        int min_ed = 9999;


        cn2py_segment  cp("init use");  
        cp.cn2py_init(results[i].query);
        int ret = cp.do_cn2py();
        if (ret < 0)
        {
            if (debug >= 2)   printf("choice 0, %s  %d do cn2py error\n",cmd.query, ret);
            build_result_confidence( results[i], 0, "py ed parse error");   
            continue;
        }
        else
        {
            for (int x=0; x< cp.py_result_number && x < 50; x++)
            {
                string str(cp.py_result_str[x], strlen(cp.py_result_str[x]) );

                for (int y=0; y<(cmd.cp)->py_result_number; y++)
                {
                    string dest( (cmd.cp)->py_result_str[y], strlen((cmd.cp)->py_result_str[y] ) );
                    int ed_len = ed.minDistance(str, dest);
                    if (ed_len == 1)
                    {
                        min_ed = 1;
                        break;
                    }
                    if (ed_len < min_ed)
                        min_ed = ed_len;
                }
            }
            if (min_ed != 1)
            {
                build_result_confidence( results[i], 0, "pyed > 1");       
                continue;
            }
        }


        if (debug>=2)  printf("result query %s\n", results[i].query);
        if (type1.en == 1 && type1.cn == 0 )
        {
            if (type2.cn == 0)
            {
                build_result_confidence( results[i], 0, "en en type");
                continue;
            }
            else
            {
                if (results[i].cooc > 0)
                    build_result_confidence( results[i], 2, "en cn, cooc>0");
                else
                    build_result_confidence( results[i], 1, "en cn, cooc=0");

            }
        }
        else if (type1.en == 0 && type1.cn == 1)
        {
            if (abs( int(max(w1_list.size(),w2_list.size()) - same_word) ) > 1  )
            {
                build_result_confidence( results[i], 0, "cn cn diff word number > 1");
                continue;
            }

            if (type2.en == 1)
            {
                build_result_confidence( results[i], 0, "cn cn type2.en ==  1");
                continue;
            }
            else
            {
                if (len1 == 4)
                    build_result_confidence( results[i], 0, "cn cn len1=4 forbidden");
                else if (len1 == 6 && query_coverage < 0.65 )
                    build_result_confidence( results[i], 0, "cn cn len1=6 coverage < 0.65");
                else if (len1 == 8 && query_coverage < 0.74 )
                    build_result_confidence( results[i], 0, "cn cn len1=8 coverage < 0.74");
                else if (query_coverage < 0.8)
                    build_result_confidence( results[i], 0, "cn cn  coverage < 0.8");
                else if (results[i].cooc > 0)
                    build_result_confidence( results[i], 2, "cn cn  cooc > 0");
                else 
                    build_result_confidence( results[i], 1, "cn cn  else");

            }
        }
        else if (type1.en == 1 &&  type1.cn == 1)
        {
            //if (abs( int(max(w1_list.size(),w2_list.size() ) - same_word )) > 1  )
            //{
            //    build_result_confidence( results[i], 0, "cn+en cn+en diff word number > 1");
            //    continue;
            //}

            if (type2.en == 1)
            {
                build_result_confidence( results[i], 0, "cn+en cn+en type2.en ==  1");
                continue;
            }
            else
            {
                if (len1 == 4)
                    build_result_confidence( results[i], 0, "cn+en cn+en len1=4 forbidden");
                else if (len1 == 6 && query_coverage < 0.65 )
                    build_result_confidence( results[i], 0, "cn+en cn+en len1=6 coverage < 0.65");
                else if (len1 == 8 && query_coverage < 0.74 )
                    build_result_confidence( results[i], 0, "cn+en cn+en len1=8 coverage < 0.74");
                else if (query_coverage < 0.74)
                    build_result_confidence( results[i], 0, "cn+en cn+en  coverage < 0.75");
                else if (results[i].cooc > 0)
                    build_result_confidence( results[i], 2, "cn+en cn+en  cooc > 0");
                else 
                    build_result_confidence( results[i], 1, "cn+en cn+en  else");

            }
        }
        else
            build_result_confidence( results[i], 0, "query error");

        if (debug>=2) printf("size %d %d\n", (int)w2_list.size(), (int)different.size());
        w2_list.clear();
        different.clear();
    }
    w1_list.clear();
    return 0;
}


int cn_ed_control_result(request & cmd, vector<correct_result_t> &results)
{
    uint64 id = first_half_md5(cmd.query, strlen(cmd.query));
    unsigned int freq = get_freq(id);
    double lm = mlang_get_flogp_sentence(mlang, cmd.query, LM_N_GRAM);

    vector<string>  w1_list;
    split_query(cmd.query, w1_list);
    ED ed;

    vector<string>  w2_list;


    for(unsigned int i=0; i<results.size(); i++)
    {
        unsigned int prop = results[i].freq / (freq + 1);

        //uint64 result_id = first_half_md5(buf, strlen(buf));
        if (freq >= 10  && prop < 100)
        {
            build_result_confidence(results[i], 0 , " freq>=10");
            continue;
        }

        if ( strcmp(cmd.query, results[i].query) == 0  )
        {
            build_result_confidence(results[i], 0 , " dup");
            continue;
        }

        if (results[i].freq == 0)
        {
            build_result_confidence(results[i], 0 , " result freq == 0");
            continue;
        }

        if (freq > results[i].freq )
        {
            build_result_confidence(results[i], 0 , "freq > result freq");
            continue;
        }

        unsigned int reverse_cooc = get_cooc(results[i].query, cmd.query);
        if (reverse_cooc > results[i].cooc + 1)
        {
            build_result_confidence(results[i], 0 , "reverse cooc  >  cooc ");
            continue;
        }

        w2_list.clear();
        split_query(results[i].query, w2_list);
        vector<string> different;
        uint32 same_word = cn_query_similar(w1_list, w2_list, different);
        uint32 difftype = diff_type(different);

        if (difftype == 8 || difftype == 4)
        {
            build_result_confidence( results[i], 0, "diff in punc or number");
            continue;
        }

        if (abs( int(max(w1_list.size(),w2_list.size()) - same_word) ) > 1  )
        {
            build_result_confidence( results[i], 0, "cn cn diff word number > 1");
            continue;
        }


        if (different.size() ==1 &&  diff_in_cnnumber(different) )
        {
            build_result_confidence( results[i], 0, "cn, cn,else,  diff in cn numbers ");
            continue;
        }

        if (different.size() ==1 &&  diff_in_stopword(different) )
        {
            build_result_confidence( results[i], 0, "cn, cn,else,  diff in stop word");
            continue;
        }

        if ( 1 == diff_in_location(w1_list, w2_list) )
        {
            build_result_confidence( results[i], 0, "diff in location");
            continue;
        }

        if (results[i].type == CN_ED_WRONG)
        {
            int ed_len = ed.minDistance( w1_list, w2_list );
            if (ed_len > 1)
            {
                build_result_confidence( results[i], 0, "ed_len > 1");
                continue;
            }

            if (strlen(cmd.query) <= 8)
            {
                build_result_confidence( results[i], 0, "cmd query len <= 8");
                continue;
            }
        }
        
        int len = strlen(cmd.query);
        int result_len = strlen( results[i].query );

        if (results[i].type != CN_ED_MISS)
        {
            if ( 1 == inner_filter_by_lm(results[i].lm, lm, len)  )
            {
                build_result_confidence(results[i], 0 , "filter by lm ");
                continue;
            }
        }
        else
        {
            double lm_com = 0;

            if (result_len > len )
            {
                lm_com = ( result_len - len )*2;
            }

            if ( 1 == inner_filter_by_lm(results[i].lm + lm_com, lm, len)  )
            {
                build_result_confidence(results[i], 0 , "filter by lm ");
                continue;
            }

        }
        
        if ( results[i].cooc > 100 )
            build_result_confidence(results[i], 3 , "cooc > 100");
        else if( results[i].cooc > 10 )
            build_result_confidence(results[i], 2 , "cooc > 10");
        else 
            build_result_confidence(results[i], 1 , "else ");
    }
    return 0;
 
}

//
// used for test pg gprof
//
void sigUsr1Handler(int sig)
{
    if (running_flag == 1)
    {
        running_flag = 0;
        return;
    }

    if (running_flag == 0)
        running_flag = 2;

    return;

    fprintf(stderr, "Exiting on SIGUSR1\n");
    void (*_mcleanup)(void);
    _mcleanup = (void (*)(void))dlsym(RTLD_DEFAULT, "_mcleanup");
    if (_mcleanup == NULL)
         fprintf(stderr, "Unable to find gprof exit hook\n");
    else _mcleanup();
    _exit(0);
}


void* request_server(void*)
{
    int listenfd = tcplisten(0, 0);
    int connfd;
    int offset;


    while (running_flag!= 2) 
    {

        connfd = tcpaccept(listenfd);

        if (connfd > 0) 
        {
            if ((offset = g_workpool.insert_item(connfd)) == -1)
            {
                p_error_log->write_log("PendingPool insert error\n");
                tcpsenderror(connfd);
                close(connfd);
                printf("insert items : overflow!\n");
            } 
            else
            {
                if (g_workpool.queue_in(offset) < 0) 
                {
                    g_workpool.work_reset_item(offset, false);
                    printf( "queen_in: overflow!\n");
                    p_error_log->write_log("PendingPool queue in error\n");
                }
            }

        } 
        else
        {
            printf( "Accept client error!");
        }
    }
    return NULL;
}





/*
    @return value means
    -1 means error
    0, no result
    1, low result
    2, mid result
    3, high result
*/
int correct(request & cmd, vector<correct_result_t> &results,  dirty_rules& dr)
{
    unsigned int len = strlen(cmd.query);
    uint64 queryid = first_half_md5(cmd.query, len) ;
    vector<correct_result_t> all_results;
    all_results.reserve(MAX_RESULT_LIST);
    uint32 results_number = 0;
    vector<correct_result_t>::iterator it ;


    correct_result_t debug_results;

    strcpy(debug_results.query, cmd.source);
    debug_results.freq = get_freq(queryid);
    debug_results.confidence = 4;
    debug_results.lm = mlang_get_flogp_sentence(mlang, cmd.query, LM_N_GRAM);
    cmd.lm = debug_results.lm;



    char manual_query[64];
    memset(manual_query, 0, sizeof(manual_query));
    uint64 queryid_source = first_half_md5(cmd.source, strlen(cmd.source)   ) ;
    if ( is_head_manual(queryid_source, manual_query) == 1 )
    {
        results.push_back(debug_results);

        correct_result_t one;

        if (strlen(manual_query) == 0 )
        {
            results[0].confidence = CR_HERD_MANUAL;
            return 0;
        }
        else
        {
            strncpy(one.query, manual_query, strlen(manual_query) );
            one.confidence = 1;
            one.type = CR_HERD_MANUAL;

            results.push_back(one);
        }
        return 1;     
    }

    query_type type;
    parse_query(cmd.query, type);

    if (type.cn == 1)
    {
        if (len > 40 || len <= 3)
        {
            debug_results.type = CR_PRE_ORDER; results.push_back(debug_results);
            return 0;
        }
    } 
    else
    {
        if (len > 50 || len <= 3)
        {
            debug_results.type = CR_PRE_ORDER; results.push_back(debug_results);
            return 0;
        }
    }
        


    char query_tmp[64];
    memset(query_tmp, 0, sizeof(query_tmp));
    strncpy(query_tmp, cmd.query, strlen(cmd.query));
    if ( dr.is_dirty_rules(query_tmp) )
    {
        results.push_back(debug_results);

        correct_result_t tmp_result;
        strncpy(tmp_result.query, query_tmp, strlen(query_tmp) );
        tmp_result.confidence = 2;
        tmp_result.type = CN_TRICK;

        results.push_back(tmp_result);
    
        return 0;  
    }    


    {
        if (is_pure_number(cmd.query))
        {
            debug_results.type = CR_PRE_ORDER_NUM; results.push_back(debug_results); 
            return 0;
        }

        if (is_baike(queryid))
        {
             debug_results.type = CR_PRE_ORDER_BAIKE; results.push_back(debug_results);  
            return 0;
        }

        if (is_location(queryid))
        {
             debug_results.type = CR_PRE_ORDER_LOCATION; results.push_back(debug_results); 
            return 0;
        }

        if (is_eng_word(queryid))
        {
             debug_results.type = CR_PRE_ORDER_ENG; results.push_back(debug_results);
            return 0;
        }

        if (is_corp_name(queryid))
        {
             debug_results.type = CR_PRE_ORDER_CORP; results.push_back(debug_results); 
            return 0;
        }

        if ( is_simple_rules(cmd.query) )
        {
            debug_results.type = CR_PRE_ORDER_SM; results.push_back(debug_results); 
            return 0;
        }

        if ( NULL != strcasestr(cmd.source, "site:") || ( NULL != strcasestr(cmd.source, "tag:") || ( NULL != strcasestr(cmd.source, "title:") )
        {
            debug_results.type = CR_PRE_ORDER_SM; results.push_back(debug_results); 
            return 0;
        }
        if (( NULL != strcasestr(cmd.source, "inurl:") ) || ( NULL != strcasestr(cmd.source, "link:") ) )
        {
            debug_results.type = CR_PRE_ORDER_SM; results.push_back(debug_results); 
            return 0;
        }

        // english blank error correct control
        fuzzy * pf =  g_fuzzy;
        vector<vector<string> > lists;
        if (type.cn==0 && type.number == 0 && type.puncation == 0 &&  strlen(cmd.source)>=7  && pf->check_english(cmd.source, lists) > 0) // english word ok
        {

            string new_query;
            // trick here. suppose only deal with first element of lists
            for(unsigned int j=0; j<lists[0].size(); j++)
            {
                new_query += lists[0][j];
                if (j != lists[0].size() -1 )
                {
                    new_query += string(" ");
                }
            }

            if (debug >= 2)  printf("new_query:--%s--\n",new_query.c_str() );
            if (debug >= 2)  printf("cmd source:--%s--\n",cmd.source );

            debug_results.type = CR_PRE_ORDER_EG; results.push_back(debug_results); 
            if ( !strncmp(cmd.source, new_query.c_str(), new_query.length())  ) // english, equayl
            {
                return 0;        
            }
            else
            {
                correct_result_t tmp_result;
                strncpy(tmp_result.query, new_query.c_str(), new_query.length() );  
                tmp_result.confidence = 2;
                tmp_result.type = EN_CR_FUZZY;

                results.push_back(tmp_result); 
                return 0;
            }
        }

        // internet telnet都被这个策略纠正了. 所以放在英文策略之后
        if (type.cn == 0 && is_pure_url(cmd.source) )
        {
    
            char  url_query[128];
            //int url_ret  = correct_url(cmd.source, url_query, 128);
            int url_ret  = correct_url(cmd.source, url_query, 128);

            debug_results.type = CR_PRE_ORDER; 
            strcpy(debug_results.query, cmd.source);
            results.push_back(debug_results);

            if ( url_ret == 1 && strncmp( url_query , cmd.source, strlen(url_query)) )
            {
                correct_result_t tmp_result;
                strncpy(tmp_result.query, url_query, strlen(url_query) );  
                tmp_result.confidence = 2;
                tmp_result.type = URL_ED;

                results.push_back(tmp_result);                      
            }

            return 0;
        }

    }



    
    
    struct timeval old_tv;
    QY_TIME;

    
    uint32 confident_flag = 0;
    // py strategy
    {

        get_correct_py(cmd.query, all_results, PY_TOTAL);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if (debug>=1) debug_msg(all_results);
        py_control_result( cmd, all_results);
        //tmp_results.clear();


        get_correct_py_fuzzy(cmd, all_results, PY_FUZZY);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if (debug>=1) debug_msg(all_results);
        py_control_result( cmd, all_results);
        if(debug>=1) debug_msg(all_results);
        print_time( old_tv, "py fuzzy", cmd.query );

        // trick here
        {
            debug_results.pylm = cmd.pylm;
            // confidence =5, means it is pure py
            if ( debug_results.pylm < 45 && type.cn == 0 && type.blank == 0 && type.puncation == 0 && type.number == 0 )
            {
                debug_results.confidence = 5;
            }
        }

    
        get_correct_py_reverse_ed(cmd.query, all_results, PY_ED_REVERSE);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if (debug>=1)debug_msg(all_results);
        py_ed_control_result( cmd, all_results);
        if (debug>=1)debug_msg(all_results);
        print_time( old_tv, "reverse py ed", cmd.query );


        get_correct_py_miss_ed(cmd.query, all_results, PY_ED_MISS);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if (debug>=1)debug_msg(all_results);
        py_ed_control_result( cmd, all_results);
        if (debug>=1)debug_msg(all_results);
        print_time( old_tv, "py miss ed", cmd.query );


        get_correct_py_add_ed(cmd.query, all_results, PY_ED_ADD);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if (debug>=1)debug_msg(all_results);
        py_ed_control_result( cmd, all_results);
        if (debug>=1)debug_msg(all_results);


        get_correct_py_wrong_ed(cmd.query, all_results, PY_ED_WRONG);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if(debug>=1) debug_msg(all_results);
        py_wrong_ed_control_result(cmd, all_results);
        if(debug>=1) debug_msg(all_results);
        print_time( old_tv, "py add and wrong ed", cmd.query );


        //  最后一个召回策略, 不用考虑confident了
        results_number = find_best_for_py( all_results, results, debug_results);
        if(debug>=1) debug_msg(results);
        if ( results_number )
        {
            it = results.begin();  results.insert(it, debug_results);
            return results_number;
        }
    }

    vector<correct_result_t> segment_results;
    get_correct_segment_total(cmd, segment_results, PY_SEGMENT_TOTAL);
    print_time( old_tv, "segment total", cmd.query );
    if (segment_results.size() > 0)
    {
        results.push_back(debug_results);
        results.push_back(segment_results[0]);
        return 1;
    }


    // cn ed strategy
    {
        all_results.clear();

        get_correct_cn_reverse_ed(cmd.query, all_results, CN_ED_REVERSE);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if(debug>=1) debug_msg(all_results);
        cn_ed_control_result(cmd, all_results);
        if(debug>=1) debug_msg(all_results);
        confident_flag = is_exist_confident_result(all_results);
        if (  confident_flag)
        {
            results_number = find_best( all_results, results);
            it = results.begin();  results.insert(it, debug_results); 
            return results_number;
        }

        //all_results.clear();
        
        get_correct_cn_miss_ed(cmd.query, all_results, CN_ED_MISS);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if(debug>=1) debug_msg(all_results);
        cn_ed_control_result(cmd, all_results);
        if(debug>=1) debug_msg(all_results);
        confident_flag = is_exist_confident_result(all_results);
        if (  confident_flag)
        {
            results_number = find_best( all_results, results);
            it = results.begin();  results.insert(it, debug_results); 
            return results_number;
        }

        //all_results.clear();
        
        get_correct_cn_wrong_ed(cmd.query, all_results, CN_ED_WRONG);
        stable_sort (all_results.begin(), all_results.end(), results_sort);
        if(debug>=1) debug_msg(all_results);
        cn_ed_control_result(cmd, all_results);
        if(debug>=1) debug_msg(all_results);
        //  最后一个召回策略, 不用考虑confident了
        results_number = find_best( all_results, results);
        if ( results_number )
        {
            it = results.begin();  results.insert(it, debug_results);
            return results_number;
        }

    }


    all_results.clear();
    get_correct_im(cmd.query, all_results, CR_IM);
    stable_sort (all_results.begin(), all_results.end(), results_sort_by_lm);
    results_number = find_best( all_results, results);
    if ( results_number )
    {
        it = results.begin();  results.insert(it, debug_results);
        return results_number;
    }   
    print_time( old_tv, "ime ", cmd.query );


    // baidu data
    memset(manual_query, 0, sizeof(manual_query));
    int ret_value = is_post_manual(queryid, manual_query);
    if (ret_value != 0 && strcasecmp ( manual_query, cmd.source ) ) //&& strcasecmp(manual_query, cmd.query) )
    {
        // 4kw的人工干预
        if ( ret_value  == 2 )
        {
            correct_result_t one;
            strncpy(one.query, manual_query, strlen(manual_query) );

            one.confidence = 1;
            one.type = CR_MANUAL;

            one.lm = mlang_get_flogp_sentence(mlang, one.query, LM_N_GRAM);

            results.push_back(one);

            char none_blank_buf[64];
            memset(none_blank_buf, 0, 64);

            unsigned int blank_num = strip_blank( one.query, none_blank_buf, strlen(one.query) );

            if ( blank_num != 0 && type.en == 1 && 0 == strcmp(none_blank_buf, cmd.source)  && strlen(none_blank_buf) == strlen(cmd.source) )
                one.type = CR_BLANK;

            //printf("%s\n", tmp_result.query);                
        }
        if (ret_value == 1) //这个是屏蔽
        {
            debug_results.confidence = CR_MANUAL;
        }
    }

    it = results.begin();  results.insert(it, debug_results);
    return 0;
}




void * child_main(void* )
{   
    int     ret = 0;    // return value
    int     handle, queuelength, wait_time;
    int     client = -1;
    //int     tindex = (int)arg;// thread index
    //char    query[64];

    //pthread_detach(pthread_self());

    
    

    dirty_rules dr;
    //results.resize(600);
    // big while
    while (running_flag) 
    {

        try
        {
            vector<correct_result_t> results;

            if (!(ret = g_workpool.work_fetch_item(handle, client, queuelength, wait_time)))
                continue;

            request cmd;

            // -1 means socket error, 0 means bad request
            int ret = tcpread_query(client, cmd) ;
            /*
                if ( -1 == ret ) // socket error
                {
                    tcpsenderror(client);
                    g_workpool.work_reset_item(handle, false);
                    continue;
                }
            */

            if (1 != ret) // bad request
            {
                tcpsendmsg(client, results, cmd.pb_flag);
                g_workpool.work_reset_item(handle, false);
                continue;
            }
            
            ret = correct(cmd, results, dr);
            tcpsendmsg(client, results, cmd.pb_flag);

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


void gettime(char * buf, unsigned int len)
{
    time_t t = time(0); 
    memset(buf, 0 , len);
    strftime( buf, len, "%Y-%m-%d %X %A \n",localtime(&t) ); 
}

void reload_dict(int sigNum)
{
    g_reload_flag = 1;
}

void turn_on_debug(int sigNum)
{
    if (debug == 0)
        debug = 2;
    else
        debug = 0;
}


void * thread_reload_dict(void* arg)
{
    //pthread_detach(pthread_self());

    while(running_flag)
    {
        char tmp[64]; 
        gettime(tmp, 64);
        if (g_reload_flag ==  1 )
        {



            int ret = g_dwh->reload(); 

            if (ret == -1)
            {
                p_status_log->write_log( tmp );
                p_status_log->write_log("FAILED load mamual_head dict\n" );
            }
            else
            {
                p_status_log->write_log( tmp );
                p_status_log->write_log("load mamual_head dict\n" );
            }
            g_reload_flag = 0;
        }
        else
        {
            sleep(1);
            p_status_log->write_log( tmp );
            p_status_log->flush_log();
            p_error_log->flush_log();
            p_log->flush_log();
        }
    }
   return NULL;
}


void log_rotate(int sigNum)
{
    p_status_log->rotate_log();
    p_error_log->rotate_log();
    p_log->rotate_log();

    char tmp[64]; 
    gettime(tmp, 64);

    p_status_log->write_log(tmp);
    p_status_log->write_log("log rotate\n");   
}


void * load_lm(void* argv)
{

    size_t pos = 0;
    mlang = mlang_load_from_file("mlang/mlang.bin", &pos);

    printf("--- mlang load finish\n");
    lattice = lattice_new();
    lattice_build(lattice, "mlang/word_freq.txt", "mlang/char.p");   
    printf("--- lattice load finish\n");
    
    return NULL; 
}


int main(int argc,char* argv[])
{

    debug = 0;
    cr_flag = 0;

    if (argc > 1)
    {
        if (strcmp(argv[1], "-1")==0)
            debug = 1;
        
        if (strcmp(argv[1], "-2")==0)
            debug = 2;

        if (strcmp(argv[1], "-s")==0)  // for safefy exit
            debug = -1;

        if (strcmp(argv[1], "-v")==0)
        {
            printf("Build version %s %s\n", __DATE__, __TIME__);
            return 0;
        }

        if (argc >= 4 && strcmp(argv[2], "-p")==0)
            g_port = atoi(argv[3]);
    }


    unsigned long stack_size = 0;
    pthread_attr_t thread_attr;
    int status = pthread_attr_init (&thread_attr);
    status = pthread_attr_getstacksize (&thread_attr, &stack_size);
    printf("stack size is %ld\n", stack_size);


    my_log status_log("log/service.log");
    p_status_log = &status_log;
    my_log error_log("log/error.log");
    p_error_log = &error_log;
    my_log lg;
    p_log = &lg;



    char tmp[64]; 
    gettime(tmp, 64);
    p_status_log->write_log(tmp );



    signal(SIGUSR2,  SIG_IGN);
    signal(SIGUSR1,  SIG_IGN);
    signal(SIGPIPE,  SIG_IGN);


    
    cn2py_segment g_dcit_cs; // init dict just here

    p_status_log->write_log("cs init over\n" );

    fuzzy f;
    g_fuzzy = (fuzzy*) &f;


    DataWarehouse dwh;

    PYLM pylm;
    g_pylm = &pylm;

    //code_convert cc_g2u("GB18030", "UTF-8");
    //code_convert cc_u2g("UTF-8", "GB18030" );
    pthread_t pt_lm;
    memset(&pt_lm, 0, sizeof(pt_lm) ) ;
    pthread_create(&pt_lm, NULL, load_lm, NULL );

    p_status_log->write_log("start load x----\n");
    dwh.init();
    p_status_log->write_log("finish load x----\n");


    pthread_join(pt_lm, NULL);

    p_status_log->write_log("dwh and lm init over\n" );

    g_dwh = (DataWarehouse *)&dwh;
    //g_g2u = (code_convert*) &cc_g2u;
    //g_u2g = (code_convert*) &cc_u2g;



    pthread_t pt[THREAD_NUMBER]; 
    memset(pt, 0, sizeof(pt));


    for (unsigned int i=0; i< THREAD_NUMBER-1; i++)
    {
        if(pthread_create(&pt[i], NULL, child_main, NULL ) != 0)
        {
            perror("pthread_create error \n");
            return -1;
        }
    }

    /*
    pthread_t reload_pt;
    if (pthread_create(&reload_pt, NULL, thread_reload_dict, NULL) != 0)
    {
        perror("pthread_create error \n");
        return -1;
    }*/
    
    p_status_log->write_log("work thread init over\n" );

    pthread_create(&pt[ THREAD_NUMBER-1 ], NULL,thread_reload_dict, NULL);

    
    if (debug != -1)
    {
        // should here, avoid reload when server starting
        signal(SIGUSR1,  reload_dict);
        signal(SIGUSR2,  log_rotate);
    }
    else
    {
        // its used for test profermance
        signal(SIGUSR1, sigUsr1Handler);
        signal(SIGUSR2,  turn_on_debug);
    }


    request_server(NULL);

    for (unsigned int i=0; i< THREAD_NUMBER; i++)
    {
        pthread_join(pt[i], NULL);
    }

    return 0;
}


