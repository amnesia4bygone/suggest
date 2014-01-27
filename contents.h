
#ifndef __CONTENTS__
#define __CONTENTS__

#include <vector>
#include "common_def.h"


using namespace std;



// 定义一个 query 和一个索引, 是什么类型匹配的.
typedef enum
{
    TYPE_NULL = 0,
    CN_MID = 1,
    CN_PRE = 3,
    PY_MID = 2,
    PY_PRE = 4
}MATCH_TYPE;


class one_query
{
public:
    uint64 id;
    uint32 doota;
    uint32 search;

    uint64 unique_id;
    MATCH_TYPE  type;
    one_query();
    
};


class contents
{
public:
    vector<one_query> lists;
    uint32 used_num; 

    // uid is used skpi querys which only diff in blank
    int insert(uint64 query_id, uint32 doota, uint32 search, uint64 uid, MATCH_TYPE  type); 
    contents();
    void debug(void);

private:
    uint32 min_doota;
    uint32 min_doota_offset;
    void find_min_offset(void);
};


typedef dense_hash_map<uint64, contents *, OwnHash> CHash;
typedef dense_hash_map<uint64, contents *, OwnHash>::iterator CHashITE;



#endif




