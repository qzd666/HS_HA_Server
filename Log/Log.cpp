#include "Log.h"
#include <cstdarg>

bool Log::mutex_inited = false;
pthread_mutex_t Log::m_mutex;

template<class T>
bool block_queue<T>::mutex_inited = false;

template<class T>
bool block_queue<T>::cond_inited = false;

template<class T>
pthread_mutex_t block_queue<T>::m_mutex;

template<class T>
pthread_cond_t block_queue<T>::m_cond;

Log::Log()
{
	m_count = 0;
	m_is_async = false;
}

Log::~Log()
{
	if (m_fp != nullptr)
	{
		fclose(m_fp);
	}
}

bool Log::init(const char* file_name, int log_buf_size, int log_lines, int max_queue_size)
{

	if (!mutex_inited)
	{
		if (pthread_mutex_init(&m_mutex, nullptr) != 0)
		{
			perror("pthread_mutex_init");
			exit(-1);
		}
		mutex_inited = true;
	}
	if (max_queue_size > 0)
	{
		m_is_async = true;
		m_log_queue = new block_queue<string>(max_queue_size);
		pthread_t tid;
		pthread_create(&tid, nullptr, flush_log_thread, nullptr);
	}

	m_buf_size = log_buf_size;
	m_buf = new char[m_buf_size];
	memset(m_buf, '\0', m_buf_size);
	m_split_lines = log_lines;

	time_t t = time(nullptr);
	struct tm* sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;

	const char* p = strrchr(file_name, '/');
	char log_full_name[256] = { 0 };
	if (p == nullptr)
	{
		snprintf(log_full_name, 255, "%d-%02d-%02d-%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
	}
	else
	{
		strcpy(log_name, p + 1);
		strncpy(dir_name, file_name, p - file_name + 1);
		snprintf(log_full_name, 255, "%s%d-%02d-%02d-%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
	}
	m_today = my_tm.tm_mday;

	m_fp = fopen(log_full_name, "a");
	if (m_fp == nullptr)
	{
		perror("fopen");
		return false;
	}
	return true;
}

void Log::write_log(int option, const char* format, ...)
{
	struct timeval now = { 0, 0 };
	gettimeofday(&now, nullptr);
	time_t t = now.tv_sec;
	struct tm* sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;
	
	char s[16] = { 0 };
	switch (option)
	{
	case 0:
		strcpy(s, "[debug]:");
		break;
	case 1:
		strcpy(s, "[info]:");
		break;
	case 2:
		strcpy(s, "[warn]:");
		break;
	case 3:
		strcpy(s, "[error]:");
		break;
	default:
		strcpy(s, "[info]:");
		break;
	}
	pthread_mutex_lock(&m_mutex);
	m_count++;
	// 创建新的日志文件
	if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
	{
		char new_log[256] = { 0 };
		flush();
		fclose(m_fp);
		char tail[16] = { 0 };
		snprintf(tail, 16, "%d-%02d-%02d-", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
		if (m_today != my_tm.tm_mday)
		{
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
			m_today = my_tm.tm_mday;
			m_count = 0;
		}
		else
		{
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
		}
		m_fp = fopen(new_log, "a");
	}
	pthread_mutex_unlock(&m_mutex);

	va_list args;
	va_start(args, format);
	string log_str;
	pthread_mutex_lock(&m_mutex);
	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06d %s ", 
		my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
		my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
	int m = vsnprintf(m_buf + n, m_buf_size - 1, format, args);
	m_buf[n + m] = '\n';
	m_buf[n + m + 1] = '\0';
	log_str = m_buf;

	pthread_mutex_unlock(&m_mutex);

	if (m_is_async && !m_log_queue->full())
	{
		m_log_queue->push(log_str);
	}
	else
	{
		pthread_mutex_lock(&m_mutex);
		fputs(log_str.c_str(), m_fp);
		pthread_mutex_unlock(&m_mutex);
	}
	va_end(args);
}

void Log::flush()
{
	pthread_mutex_lock(&m_mutex);
	fflush(m_fp);
	pthread_mutex_unlock(&m_mutex);
}
