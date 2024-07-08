#pragma once
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

template<class T>
class block_queue
{
public:
	block_queue(int max_size = 1000)
	{
		if (max_size <= 0)
			exit(-1);
		m_max_size = max_size;
		m_array = new T[max_size];
		m_size = 0;
		m_front = -1;
		m_back = -1;
		if (!mutex_inited)
		{
			if (pthread_mutex_init(&m_mutex, nullptr) != 0)
			{
				perror("pthread_mutex_init");
				exit(1);
			}
			mutex_inited = true;
		}
		if (!cond_inited)
		{
			if (pthread_cond_init(&m_cond, NULL) != 0)
			{
				perror("pthread_cond_init");
				exit(1);
			}
			cond_inited = true;
		}
	}

	void clear()
	{
		pthread_mutex_lock(&m_mutex);
		m_max_size = 0;
		m_size = 0;
		m_front = -1;
		m_back = -1;
		pthread_mutex_unlock(&m_mutex);
	}

	~block_queue()
	{
		pthread_mutex_lock(&m_mutex);
		if (m_array)
		{
			delete[] m_array;
		}
		pthread_mutex_unlock(&m_mutex);
	}

	int size()
	{
		int tmp = 0;
		pthread_mutex_lock(&m_mutex);
		tmp = m_size;
		pthread_mutex_unlock(&m_mutex);

		return tmp;
	}

	int max_size()
	{
		int tmp = 0;
		pthread_mutex_lock(&m_mutex);
		tmp = m_max_size;
		pthread_mutex_unlock(&m_mutex);

		return tmp;
	}

	bool get_front(T &value)
	{
		pthread_mutex_lock(&m_mutex);
		if (m_size == 0)
		{
			pthread_mutex_unlock(&m_mutex);
			return false;
		}
		value = m_array[m_front];
		pthread_mutex_unlock(&m_mutex);
		return true;
	}

	bool get_back(T &value)
	{
		pthread_mutex_lock(&m_mutex);
		if (m_size == 0)
		{
			pthread_mutex_unlock(&m_mutex);
			return false;
		}
		value = m_array[m_back];
		pthread_mutex_unlock(&m_mutex);
		return true;
	}

	bool full()
	{
		pthread_mutex_lock(&m_mutex);
		if (m_size >= m_max_size)
		{
			pthread_mutex_unlock(&m_mutex);
			return true;
		}
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	bool empty()
	{
		pthread_mutex_lock(&m_mutex);
		if (m_size == 0)
		{
			pthread_mutex_unlock(&m_mutex);
			return true;
		}
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	bool push(const T &item)
	{
		pthread_mutex_lock(&m_mutex);
		if (m_size >= m_max_size)
		{
			pthread_cond_broadcast(&m_cond);
			pthread_mutex_unlock(&m_mutex);
			return false;
		}
		m_back = (m_back + 1) % m_max_size;
		m_array[m_back] = item;
		m_size++;

		pthread_cond_broadcast(&m_cond);
		pthread_mutex_unlock(&m_mutex);

		return true;
	}

	bool pop(T &item)
	{
		pthread_mutex_lock(&m_mutex);
		while (m_size <= 0)
		{
			if (!pthread_cond_wait(&m_cond, &m_mutex))
			{
				pthread_mutex_unlock(&m_mutex);
				return false;
			}
		}
		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;

		pthread_mutex_unlock(&m_mutex);
		return true;
	}

	bool pop(T &item, int timeout)
	{
		struct timespec ts = { 0, 0 };
		struct timeval tv = { 0, 0 };
		gettimeofday(&tv, nullptr);
		pthread_mutex_lock(&m_mutex);
		if (m_size <= 0)
		{
			ts.tv_sec = tv.tv_sec + timeout / 1000;
			ts.tv_nsec = (tv.tv_usec + timeout % 1000) * 1000;
			if (!pthread_cond_timedwait(&m_cond, &m_mutex, &ts))
			{
				pthread_mutex_unlock(&m_mutex);
				return false;
			}
		}
		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;

		pthread_mutex_unlock(&m_mutex);
		return true;
	}

private:
	int m_size;
	int m_max_size;
	int m_front;
	int m_back;
	T* m_array;

	static pthread_mutex_t m_mutex;
	static pthread_cond_t m_cond;
	static bool mutex_inited;
	static bool cond_inited;
};
