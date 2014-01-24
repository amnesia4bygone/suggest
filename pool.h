#ifndef __HEAD_PENDINGPOOL_
#define __HEAD_PENDINGPOOL_

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

class PendingPool {
  public:
	PendingPool();
	bool work_fetch_item(int &handle, int &sock, int &queuelen, int &stay_time);
	void work_reset_item(int handle, bool bKeepAlive); 
	int mask(fd_set * pfs);
	int close_not_busy_socket();
	int insert_item(int sock_work); 
	void check_item(fd_set * pfs);
	int queue_in(int offset);
	void set_timeout(int sec);
	int set_queuelen(int len);
	int set_socknum(int num);
	int get_freethread();
	int get_queuelen();
	int check_valid();
	
	enum { MAX_SOCK = 1500 };
	enum { QUEUE_LEN = 1000 };
	enum { DEFAULT_TIMEOUT_SEC = 5 };
	typedef enum { NOT_USED = 0, READY, BUSY } eumStatus;
  private:
	struct SItem {
		int nSock;
		int nLastActive;
		int status;
		timeval arriveTime;	
	};

	int   m_socketNum;
	SItem *m_aySocket;

	int m_queueLen;
	int *m_ayReady;
	int m_nGet;
	int m_nPut;

	int m_nFreeThread;
	int m_nTimeOut;				// connection timeout value, in seconds

	pthread_mutex_t m_mutex;
	pthread_cond_t m_condition;

	pthread_mutex_t m_mutexCPU;
	pthread_cond_t m_condCPU;
	int m_nMaxSock;
};
#endif
