
#ifndef __CONTENTS__
#define __CONTENTS__


#include "common_def.h"

class contents
{
public:
    uint64 lists[32];
    uint32 doota_num[32];  // 0, mean
    uint32 search_num[32];  // 0, mean

    uint32 used_num; 


    int insert(uint64 query_id, uint32 doota, uint32 search);
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




