
#ifndef __CONTENTS__
#define __CONTENTS__

#include <vector>
#include "common_def.h"


using namespace std;



class one_query
{
public:
    uint64 id;
    uint32 doota;
    uint32 search;

    uint64 unique_id;
    one_query();
    
};


class contents
{
public:
    vector<one_query> lists;
    uint32 used_num; 

    int insert(uint64 query_id, uint32 doota, uint32 search, uint64 uid); // uid is used skpi querys which only diff in blank
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




