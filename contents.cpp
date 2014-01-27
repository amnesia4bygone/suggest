

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#include <algorithm>

#include "contents.h"



bool vector_comp (one_query i, one_query j) 
{
    if (i.type > j.type)
        return true;
    else if  (i.type < j.type)
        return false;
    else
    {
        // doota都很丰富或者都很贫瘠, 就看频次去吧
        if( (i.doota >= 320 && j.doota >= 320)  ||  (i.doota < 20 && j.doota < 20) )
        {
            return (i.search > j.search);
        } 
        else
            return (i.doota > j.doota); 
    }
}


one_query::one_query()
{
    // careful use it, when it is poly stat
    memset(this, 0, sizeof(one_query) );
    type = TYPE_NULL;
}


contents::contents()
{
    lists.resize(32);

    used_num =0;
    min_doota = 0;
    min_doota_offset = 0;
}



void contents::debug(void)
{
    printf("-----------min %d %d %d----------------------------------\n",used_num, min_doota, min_doota_offset);
    for (unsigned int i=0; i<used_num; i++)
        printf("- %lld, %d, %d\n", lists[i].id, lists[i].doota, lists[i].search );
    printf("---------------------------------------------\n");
}


void  contents::find_min_offset(void)
{
    unsigned int tmp_doota = 9999999;

    for(uint32 i=0; i< used_num; i++ )
    {
        if (tmp_doota > lists[i].doota )
        {
            tmp_doota = lists[i].doota;
            min_doota_offset = i;
        }
    } 
    min_doota = lists[min_doota_offset].doota;  
}



// -1, error
// 0, skip
// 1, success insert
int contents::insert(uint64 query_id, uint32 doota, uint32 search, uint64 uid,  MATCH_TYPE  type)
{

    if (used_num < 32)
    {
        lists[used_num].id = query_id;
        lists[used_num].doota = doota;
        lists[used_num].search = search;
        lists[used_num].unique_id = uid;
        lists[used_num].type = type;
        used_num++;

        sort(lists.begin(), lists.end(), vector_comp);
        find_min_offset();

        return 1;
    }


    if (doota <= min_doota )
        return 0;

    lists[min_doota_offset].id = query_id;
    lists[min_doota_offset].doota = doota;
    lists[min_doota_offset].search = search;  
    lists[min_doota_offset].unique_id = uid;  
    lists[min_doota_offset].type = type;  
    
    sort(lists.begin(), lists.end(), vector_comp);
    find_min_offset();
    return 1;

}


