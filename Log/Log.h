#pragma once
#include <cstdio>
#include <string>
#include <string.h>
#include "block_queue.h"

using namespace std;
class Log
{
public:
	static Log* get_instance()
	{
		static Log instance;
		return &instance;
	}

	bool init(const char* file_name, int log_buf_size = 8192, int log_lines = 50000, int max_queue_size = 0);

	static void* flush_log_thread(void* args)
	{
		Log::get_instance()->async_write_log();
	}

	void write_log(int option, const char* format, ...);

	void flush();


private:
	Log();
	~Log();

	void* async_write_log()
	{
		string single_log;
		while (m_log_queue->pop(single_log))
		{
			pthread_mutex_lock(&m_mutex);
			fputs(single_log.c_str(), m_fp);
			pthread_mutex_unlock(&m_mutex);
		}
	}
private:
	char dir_name[128];
	char log_name[128];
	int m_split_lines;	// 日志最大行数
	int m_buf_size;
	long long m_count;	// 日志行数
	int m_today;
	FILE* m_fp;
	char* m_buf;
	block_queue<string>* m_log_queue;
	bool m_is_async;	// 是否同步
	static pthread_mutex_t m_mutex;
	static bool mutex_inited;
};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)