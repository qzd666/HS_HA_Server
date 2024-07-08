#pragma once
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include "../ConSQL/mysql_conn.h"

#define READBUF_SIZE 2048
#define WRITEBUF_SIZE 1024

class http_conn
{
public:
	enum METHOD
	{
		GET,
		POST
	};
	enum CHECK_STATUS
	{
		CHECK_REQUESTLINE,
		CHECK_HEADER,
		CHECK_CONTENT
	};
	enum HTTP_CODE
	{
		GET_REQUEST,	// ��������
		BAD_REQUEST,	// ��������
		NO_REQUEST,		// ����������
		FORBIDDEN_REQUEST,	// ��Դ��ֹ����
		NO_RESOURCE,	// ��Դ������
		FILE_REQUEST,	// ��Դ�ɷ���
		INTERNAL_ERROR	// �������ڲ�����
	};
	enum LINESTATUS
	{
		LINE_OK,
		LINE_OPEN,
		LINE_BAD
	};

	MYSQL* m_mysql;
	static int m_epollfd;
	static int userNum;
public:
	http_conn() {}
	~http_conn() {}
	void process();
	bool read();
	bool write();
	void init(int sockfd, const sockaddr_in &addr);
	void sql_result_init(sql_conn* connPool);
	void closeconn();

	inline int getSockFd() { return m_sockfd; }
	inline bool getLinger() { return m_linger; }
	sockaddr_in *get_address() { return &m_address; }

private:
	static void mutex_init();
	static void mutex_lock() { pthread_mutex_lock(&H_mutex); }
	static void mutex_unlock() { pthread_mutex_unlock(&H_mutex); }
	void init();
	inline char* getLine() { return m_readBuf + m_start_idx; }
	LINESTATUS parse_line();
	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_header(char* text);
	HTTP_CODE parse_content(char* text);
	HTTP_CODE process_read();
	bool process_write(HTTP_CODE ret);

	HTTP_CODE do_request();
	bool add_response(const char* format, ...);
	bool add_status_line(int status, const char* title);
	bool add_headers(int content_length);
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_content_type();
	bool add_content(const char* content);
	bool add_blank_line();

	void unmap();
private:
	CHECK_STATUS m_check_status;
	LINESTATUS m_status;

	int m_sockfd;
	sockaddr_in m_address;
	char m_readBuf[READBUF_SIZE];
	int m_checked_idx;	// �ѽ������ֽ���
	int m_read_idx;		// �Ѷ�ȡ���ֽ���
	int m_start_idx;	// ���http��������
	char m_writeBuf[WRITEBUF_SIZE];
	int m_write_idx;
	int bytes_to_send;
	int bytes_have_send;

	METHOD m_method;
	char* m_url;
	char* m_version;
	char* m_file_address;
	char* m_host;
	char* m_content;
	bool m_linger;	// �Ƿ񱣳�����
	struct stat m_file_stat;
	struct iovec m_iovec[2];
	int m_iovec_count;
	int m_content_length;

	int cgi; // Ϊ1����post����
	static pthread_mutex_t H_mutex;
};
