#pragma once
#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <list>
#include "../ConSQL/mysql_conn.h"
using namespace std;

template<class T>
class Threadpool
{
public:
	Threadpool(sql_conn* Sql_Conn, int threadNum, int workerMaxNum);
	~Threadpool();
	bool append(T* request);
private:
	static void* worker(void* arg);
	void run();
private:
	unsigned int m_threadNum;
	pthread_t* m_threads;
	list<T*> m_workerlist;
	unsigned int m_workerMaxNum;

	pthread_mutex_t m_mutex;
	sem_t m_sem;

	sql_conn* m_sqlconn;
};

template<class T>
Threadpool<T>::Threadpool(sql_conn* Sql_Conn, int threadNum, int workerMaxNum)
{
	if (threadNum <= 0 || workerMaxNum <= 0)
	{
		throw exception();
	}
	m_sqlconn = Sql_Conn;
	m_threadNum = threadNum;
	m_workerMaxNum = workerMaxNum;
	m_threads = nullptr;
	if (pthread_mutex_init(&m_mutex, NULL) != 0)
	{
		perror("pthread_mutex_init");
		exit(1);
	}
	if (sem_init(&m_sem, 0, 0) != 0)
	{
		perror("sem_init");
		exit(1);
	}
	m_threads = new pthread_t[threadNum];
	if (!m_threads)
	{
		throw exception();
	}
	for (int i = 0; i < threadNum; ++i)
	{
		if (pthread_create(m_threads + i, NULL, worker, this) != 0)
		{
			delete[] m_threads;
			throw exception();
		}
		if (pthread_detach(m_threads[i]) != 0)
		{
			delete[] m_threads;
			throw exception();
		}
	}
}

template<class T>
Threadpool<T>::~Threadpool()
{
	delete[] m_threads;
}

template<class T>
bool Threadpool<T>::append(T* request)
{
	if (!request)
		return false;
	pthread_mutex_lock(&m_mutex);
	if (m_workerlist.size() >= m_workerMaxNum)
	{
		pthread_mutex_unlock(&m_mutex);
		return false;
	}
	m_workerlist.push_back(request);
	pthread_mutex_unlock(&m_mutex);
	sem_post(&m_sem);
	return true;
}

template<class T>
void* Threadpool<T>::worker(void* arg)
{
	Threadpool* pool = (Threadpool*)arg;
	pool->run();
	return pool;
}

template<class T>
void Threadpool<T>::run()
{
	while (1)
	{
		sem_wait(&m_sem);
		pthread_mutex_lock(&m_mutex);
		if (m_workerlist.empty())
		{
			pthread_mutex_unlock(&m_mutex);
			continue;
		}
		T* request = m_workerlist.front();
		m_workerlist.pop_front();
		pthread_mutex_unlock(&m_mutex);

		if (!request)
		{
			continue;
		}
		sql_connRAII mysql(&request->m_mysql, m_sqlconn);
		// 处理用户请求
		request->process();
	}
}
