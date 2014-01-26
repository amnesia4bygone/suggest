

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#include "contents.h"

contents::contents()
{
    memset(lists, 0, sizeof(lists) );
    memset(doota_num, 0, sizeof(doota_num) );
    memset(search_num, 0, sizeof(search_num) );

    used_num =0;
    min_doota = 0;
    min_doota_offset = 0;
}



void contents::debug(void)
{
    printf("-----------min %d %d %d----------------------------------\n",used_num, min_doota, min_doota_offset);
    for (unsigned int i=0; i<used_num; i++)
        printf("- %lld, %d, %d\n", lists[i], doota_num[i], search_num[i]);
    printf("---------------------------------------------\n");
}


void  contents::find_min_offset(void)
{
    unsigned int tmp_doota = 9999999;

    for(uint32 i=0; i< used_num; i++ )
    {
        if (tmp_doota > doota_num[i] )
        {
            tmp_doota = doota_num[i];
            min_doota_offset = i;
        }
    } 
    min_doota = doota_num[min_doota_offset];  
}



// -1, error
// 0, skip
// 1, success insert
int contents::insert(uint64 query_id, uint32 doota, uint32 search)
{

    if (used_num < 32)
    {
        lists[used_num] = query_id;
        doota_num[used_num] = doota;
        search_num[used_num] = search;
        used_num++;

        find_min_offset();
        return 1;
    }


    if (doota <= min_doota )
        return 0;

    lists[min_doota_offset] = query_id;
    doota_num[min_doota_offset] = doota;
    search_num[min_doota_offset] = search;    
    find_min_offset();
    return 1;

}


