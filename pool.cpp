#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pool.h>

PendingPool::PendingPool()
{
	m_nGet = 0;
	m_nPut = 0;
	m_nMaxSock = 0;
	m_nFreeThread = 0;
	m_nTimeOut = DEFAULT_TIMEOUT_SEC;
	pthread_mutex_init(&m_mutex, NULL);
	pthread_cond_init(&m_condition, NULL);
	pthread_mutex_init(&m_mutexCPU, NULL);
	pthread_cond_init(&m_condCPU, NULL);
	m_queueLen = QUEUE_LEN;
	m_socketNum = MAX_SOCK;
	m_ayReady = (int*)calloc(m_queueLen, sizeof(int));
	if (m_ayReady)
	{		
		
		m_aySocket = (SItem *)calloc(m_socketNum, sizeof(SItem));
		if (m_aySocket)
		{
			for (int i = 0; i < m_socketNum; i++) {
				m_aySocket[i].nSock = -1;
				m_aySocket[i].status = NOT_USED;
			}	
		}

	
	}
}
int PendingPool::check_valid()
{
	if (!m_ayReady || !m_aySocket)
		return -1;
	return 0;	
}
int PendingPool::get_queuelen()
{
	int queuelen = m_nPut - m_nGet;
	if (queuelen < 0)
	     queuelen += m_queueLen;
	return queuelen;     
}
int PendingPool::set_queuelen(int len)
{
	if (m_ayReady)
		free (m_ayReady);
	if (len > 2)
		m_queueLen = len;
	else
		m_queueLen = 2;
	m_ayReady = (int*)calloc(m_queueLen, sizeof(int));
	if (!m_ayReady)
		return -1;
	else
		return 0;	
		
}

int PendingPool::set_socknum(int num)
{
	if (m_aySocket)
		free (m_aySocket);
	if (num > 0)
		m_socketNum = num;
	else
		m_socketNum = 1;
	m_aySocket = (SItem *)calloc(m_socketNum, sizeof(SItem));
	if (!m_aySocket)
		return -1;
	else
		return 0;	
}

bool PendingPool::work_fetch_item(int &handle, int &sock, int &queuelen, int &stay_time)
{
	bool ret = true;
	int nIndex = -1;
	timeval tv;

	handle = -1;

	pthread_mutex_lock(&m_mutex);


	while (m_nGet == m_nPut)
	{
		++m_nFreeThread;
		pthread_cond_wait(&m_condition, &m_mutex);
		--m_nFreeThread;
	}

	//--m_nFreeThread;
	nIndex = m_ayReady[m_nGet];

	m_aySocket[nIndex].status = BUSY;
	sock = m_aySocket[nIndex].nSock;
	handle = nIndex;

	if (++m_nGet >= m_queueLen)
		m_nGet = 0;
	queuelen = m_nPut - m_nGet;
	if (queuelen < 0)
	     queuelen += m_queueLen;
	
	gettimeofday(&tv, NULL);
	stay_time = (tv.tv_sec - m_aySocket[nIndex].arriveTime.tv_sec) * 1000000 
					+ (tv.tv_usec - m_aySocket[nIndex].arriveTime.tv_usec);

	pthread_mutex_unlock(&m_mutex);
	return ret;
}


void PendingPool::work_reset_item(int handle, bool bKeepAlive)
{   
	if (handle < 0)
		return;
	
	int index = handle;
	sockaddr_in newname;
	socklen_t newname_len = sizeof(newname);
	char addrstr[INET_ADDRSTRLEN];

	if (!bKeepAlive)
	{
		if (m_aySocket[index].nSock > 0)
		{
			if (0 == getpeername(m_aySocket[index].nSock,
						(struct sockaddr *)&newname, &newname_len)
					&& NULL != inet_ntop(AF_INET,
						&newname.sin_addr, addrstr, sizeof(addrstr)))
			{
				//printf( "close socket[%d] from pendingpool!"
				//		" peer information: IP[%s] PORT[%hd]", m_aySocket[index].nSock,
				//		addrstr, ntohs(newname.sin_port));
			}
			else
			{
				// printf("close socket[%d] from pendingpool!"
				//		 " get peer information failure", m_aySocket[index].nSock);
			}		   
			close(m_aySocket[index].nSock);
			m_aySocket[index].nSock = -1;
		}
		m_aySocket[index].status = NOT_USED;
	} else {
		if (m_aySocket[index].status == BUSY) {
			m_aySocket[index].status = READY;
		}
	}
}  
int PendingPool::mask(fd_set * pfs)
{
	int i;
	int largest = -1;
	int nReady = 0;
	int nBusy = 0;
	int sock;
	for (i = 0; i < m_nMaxSock; ++i) {
		if (m_aySocket[i].status == READY) {
			++nReady;
			sock = m_aySocket[i].nSock;
			FD_SET(sock, pfs);
			if (sock > largest)
				largest = sock;
		} else if (m_aySocket[i].status == BUSY) {
			++nBusy;
		}
	}

	if (nBusy > 0 && nReady == 0) {
		//printf("%d socket READY, %d BUSY, %d Total",
		//			nReady, nBusy, m_nMaxSock);
	}
	return largest + 1;
}

int PendingPool::close_not_busy_socket()
{
	int i;
	int nReady = 0;

	for (i = 0; i < m_nMaxSock; ++i) {
		if (m_aySocket[i].status == READY) {
			++nReady;
			work_reset_item(i, false);
		}
	}
	return nReady;
}

int PendingPool::insert_item(int sock_work)
{
	int i;
	timeval tv;
	sockaddr_in newname;
	socklen_t newname_len;
	char addrstr[INET_ADDRSTRLEN];
	int ret;
	const char *str = NULL;
	gettimeofday(&tv, NULL);
	newname_len = sizeof(newname);
	ret = getpeername(sock_work, (struct sockaddr *)&newname, &newname_len);
	str = inet_ntop(AF_INET, &newname.sin_addr,	addrstr, sizeof(addrstr));
	
	// insert in old place
	for (i = 0; i < m_nMaxSock; ++i) 
	{
		if (m_aySocket[i].status == NOT_USED) 
		{
			m_aySocket[i].nSock = sock_work;
			m_aySocket[i].status = READY;
			m_aySocket[i].nLastActive = tv.tv_sec;
			break;
		}
	}

	if (i == m_nMaxSock) 
	{
		if (m_nMaxSock < m_socketNum) 
		{
			++m_nMaxSock;
			m_aySocket[i].nSock = sock_work;
			m_aySocket[i].status = READY;
			m_aySocket[i].nLastActive = tv.tv_sec;
		} 
		else
		{
			if(0 == ret && str != NULL)
			{
				//printf("Insert new socket[%d] to pendingpool" 
				//		" failure! peer information: IP[%s] PORT[%hd]", sock_work, 
				//		addrstr, ntohs(newname.sin_port));
			}
			else
			{
				//printf("Insert new socket[%d] to pendingpool"
				//		" failure! get peer information failure", sock_work);
			}
			return -1;
		}
	}
	if(0 == ret && str != NULL)
	{
		//printf("Insert new socket[%d] to pendingpool"
		//		" success! peer information: IP[%s] PORT[%hd]", sock_work, 
		//		addrstr, ntohs(newname.sin_port));
	}
	else
	{
		//printf("Insert new socket[%d] to pendingpool" 
		//		" success! get peer information failure", sock_work);
	}
	return i;
}


int PendingPool::queue_in(int offset)
{
	int nSec, rtn = 0;
	timeval tv;
	gettimeofday(&tv, NULL);
	nSec = tv.tv_sec;
        //int sock_work = 0;


	pthread_mutex_lock(&m_mutex);
	m_aySocket[offset].nLastActive = nSec;
	m_aySocket[offset].arriveTime = tv;
	m_ayReady[m_nPut] = offset;
	//sock_work = m_aySocket[offset].nSock;

	if (++m_nPut >= m_queueLen)
		m_nPut = 0;
	if (m_nPut == m_nGet) {
		//printf("Buffer overflow: Buf[%d]=%d.", m_nPut, offset);
		if (--m_nPut < 0)
			m_nPut = m_queueLen - 1;
		rtn = -1;
	} else {
		m_aySocket[offset].status = BUSY;
		pthread_cond_signal(&m_condition);
		//printf("Ready %d sockets: handle %d(%d), signal sent. %d free threads",
		//			(m_queueLen + m_nPut - m_nGet) % m_queueLen,
		//			offset, sock_work, m_nFreeThread);
	}
	pthread_mutex_unlock(&m_mutex);
	return rtn;
}

void PendingPool::check_item(fd_set * pfs)
{
	int i;
	int nSec;
	timeval tv;
	gettimeofday(&tv, NULL);
	nSec = tv.tv_sec;
	int sock_work;

	for (i = 0; i < m_nMaxSock; ++i) {
		sock_work = m_aySocket[i].nSock;
		if (m_aySocket[i].status == BUSY)
			m_aySocket[i].nLastActive = nSec;
		else if (m_aySocket[i].status == READY) {
			if (FD_ISSET(sock_work, pfs)) {
				if (queue_in(i) < 0)
					work_reset_item(i, false);
			} else if (nSec - m_aySocket[i].nLastActive > m_nTimeOut) {
				//printf("[get query] socket %d, handle %d timeout",
				//			sock_work, i);
				work_reset_item(i, false);
				if (i == m_nMaxSock - 1)
					--m_nMaxSock;
			}
		}
	}
}

int PendingPool::get_freethread()
{
	return m_nFreeThread;
}

void PendingPool::set_timeout(int sec)
{
	m_nTimeOut = sec;
}
