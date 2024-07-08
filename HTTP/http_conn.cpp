#include "http_conn.h"
#include "../Log/Log.h"
#include <map>
#include <algorithm>
#include <sys/mman.h>
#include <pthread.h>
#include <stdarg.h>
#include <string>
using namespace std;

//#define connfdET // 边沿
#define connfdLT // 水平

//#define listenfdET
#define listenfdLT

const char* ROOT = "/home/qzd666/My_Server/web1/root";
map<string, string> users;
bool MUTEX_INIT = false;

//定义http响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

pthread_mutex_t http_conn::H_mutex;

int setnonblocking(int fd) {
	int flags;

	// 获取文件描述符的当前标志
	flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		return -1;
	}

	// 设置非阻塞标志
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		return -1;
	}

	return 0;
}

void addfd(int epollfd, int sockfd, bool one_shot)
{
	epoll_event event;
	memset(&event, 0, sizeof(event));
	event.data.fd = sockfd;
#ifdef connfdLT
	event.events = EPOLLIN | EPOLLRDHUP;
#endif
	
#ifdef listenfdLT
	event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef connfdET
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdET
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif
	if (one_shot)
	{
		event.events |= EPOLLONESHOT;
	}
	int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event);
	if (ret == -1)
	{
		perror("epoll_ctl");
	}
	setnonblocking(sockfd);
}

void removefd(int epollfd, int sockfd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
	close(sockfd);
}

void modfd(int epollfd, int sockfd, int ev)
{
	epoll_event event;
	event.data.fd = sockfd;
	event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#ifdef connfdET
	event.events |= EPOLLET;
#endif
	epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
}

int http_conn::userNum = 0;
int http_conn::m_epollfd = -1;

void http_conn::mutex_init()
{
	if (MUTEX_INIT == false)
	{
		MUTEX_INIT = true;
		if (pthread_mutex_init(&H_mutex, NULL) != 0)
		{
			perror("mutex_init");
			exit(1);
		}
	}
}

void http_conn::init()
{
	mutex_init();
	m_mysql = nullptr;
	m_method = GET;
	m_check_status = CHECK_REQUESTLINE;
	m_status = LINE_OPEN;
	
	m_start_idx = 0;
	m_checked_idx = 0;
	m_read_idx = 0;
	m_write_idx = 0;
	bytes_to_send = 0;
	bytes_have_send = 0;

	m_url = 0;
	m_version = 0;
	m_file_address = 0;
	m_host = 0;
	m_content = 0;
	m_content_length = 0;
	m_linger = false;
	cgi = 0;

	//memset(m_readBuf, '\0', READBUF_SIZE);
	//memset(m_writeBuf, '\0', WRITEBUF_SIZE);
	fill(m_readBuf, m_readBuf + READBUF_SIZE, '\0');
	fill(m_writeBuf, m_writeBuf + WRITEBUF_SIZE, '\0');
}


void http_conn::init(int sockfd, const sockaddr_in& addr)
{
	m_sockfd = sockfd;
	m_address = addr;
	addfd(m_epollfd, sockfd, true);
	userNum++;

	init();
}

void http_conn::sql_result_init(sql_conn* connPool)
{
	MYSQL* mysql = nullptr;
	sql_connRAII mysqlRAII(&mysql, connPool);
	
	if (mysql_query(mysql, "select ID, password from user;") != 0)
	{
		//LOG_ERROR("SELECT ERROR:%s\n", mysql_error(mysql));
		perror("mysql_query");
		//exit(1);
	}
	
	MYSQL_RES* result = mysql_store_result(mysql);
	while (MYSQL_ROW row = mysql_fetch_row(result))
	{
		string id(row[0]);
		string password(row[1]);
		users[id] = password;
	}

}

void http_conn::closeconn()
{
	if (m_sockfd != -1)
	{
		removefd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		userNum--;
	}
}

http_conn::LINESTATUS http_conn::parse_line()
{
	for (; m_checked_idx < m_read_idx; ++m_checked_idx)
	{
		char temp = m_readBuf[m_checked_idx];
		if (temp == '\r')
		{
			if ((m_checked_idx + 1) == m_read_idx)
			{
				// 下次遍历就是从 m_checked_idx+1 开始了
				return LINE_OPEN;
			}
			if (m_readBuf[m_checked_idx + 1] == '\n')
			{
				// 从'\n'下一个字符开始
				m_readBuf[m_checked_idx++] = '\0';
				m_readBuf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			else
			{
				return LINE_BAD;
			}
		}
		else if (temp == '\n')
		{
			if (m_checked_idx > 1 && m_readBuf[m_checked_idx - 1] == '\r')
			{
				m_readBuf[m_checked_idx - 1] = '\0';
				m_readBuf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			else
			{
				return LINE_BAD;
			}
		}
	}
	return LINE_OPEN;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
	m_url = strpbrk(text, " \t");
	if (!m_url)
	{
		return BAD_REQUEST;
	}
	*m_url++ = '\0';
	m_url += strspn(m_url, " \t");

	char* method = text;
	// 目前只接受GET和POST
	if (strcasecmp(method, "GET") == 0)
	{
		m_method = GET;
	}
	else if (strcasecmp(method, "POST") == 0)
	{
		cgi = 1;
		m_method = POST;
	}
	else
	{
		return BAD_REQUEST;
	}

	m_version = strpbrk(m_url, " \t");
	*m_version++ = '\0';
	m_version += strspn(m_version, " \t");
	if (strcasecmp(m_version, "HTTP/1.1") != 0)
	{
		return BAD_REQUEST;
	}

	if (strncasecmp(m_url, "http://", 7) == 0)
	{
		m_url += 7;
		m_url = strchr(m_url, '/');
	}
	else if (strncasecmp(m_url, "https://", 8) == 0)
	{
		m_url += 8;
		m_url = strchr(m_url, '/');
	}
	if (!m_url || m_url[0] != '/')
		return BAD_REQUEST;

	if (strlen(m_url) == 1)
	{
		//单独处理 m_url为'/'的情况
		//strcat(m_url, "默认文件")；
		strcat(m_url, "judge.html");
	}

	m_check_status = CHECK_HEADER;
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_header(char* text)
{
	// 一次请求一行
	if (text[0] == '\0')
	{
		if (m_content_length != 0)
		{
			m_check_status = CHECK_CONTENT;
			return NO_REQUEST;
		}
		else
			return GET_REQUEST;
	}
	else if (strncasecmp(text, "Connection:", 11) == 0)
	{
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0)
		{
			m_linger = true;
		}
	}
	else if (strncasecmp(text, "Content-length:", 15) == 0)
	{
		text += 15;
		text += strspn(text, " \t");
		m_content_length = atol(text);
	}
	else if (strncasecmp(text, "Host:", 5) == 0)
	{
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	//else
		//printf("unknown header: %s\n", text);
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
	// 目前请求体只有ID和password
	if (m_read_idx >= (m_content_length + m_checked_idx))
	{
		text[m_content_length] = '\0';
		m_content = text;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
	LINESTATUS line_status = LINE_OK;
	HTTP_CODE http_code = NO_REQUEST;
	bool content_over = false;
	char* text = 0;
	// 如果前面的条件满足了，后面的parse_line()就不会执行
	while ((m_check_status == CHECK_CONTENT && content_over == false) || ((line_status = parse_line()) == LINE_OK))
	{
		text = getLine();
		LOG_INFO("%s", text);
		Log::get_instance()->flush();
		m_start_idx = m_checked_idx;
		switch (m_check_status)
		{
		case CHECK_REQUESTLINE:
		{
			http_code = parse_request_line(text);
			if (http_code == BAD_REQUEST)
			{
				printf("request_line: bad request\n");
				return BAD_REQUEST;
			}
			break;
		}
		case CHECK_HEADER:
		{
			http_code = parse_header(text);
			if (http_code == BAD_REQUEST)
			{
				printf("header: bad request\n");
				return BAD_REQUEST;
			}
			else if (http_code == GET_REQUEST)
			{
				// 发出http响应
				return do_request();
			}
			break;
		}
		case CHECK_CONTENT:
		{
			http_code = parse_content(text);
			if (http_code == GET_REQUEST)
				return do_request();
			content_over = true;
			break;
		}
		default:
			return INTERNAL_ERROR;
		}
	}
	return NO_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret)
{
	switch (ret)
	{
	case INTERNAL_ERROR:
	{
		add_status_line(500, error_500_title);
		add_headers(strlen(error_500_form));
		if (!add_content(error_500_form))
			return false;
		break;
	}
	case BAD_REQUEST:
	{
		add_status_line(404, error_404_title);
		add_headers(strlen(error_404_form));
		if (!add_content(error_404_form))
			return false;
		break;
	}
	case FORBIDDEN_REQUEST:
	{
		add_status_line(403, error_403_title);
		add_headers(strlen(error_403_form));
		if (!add_content(error_403_form))
			return false;
		break;
	}
	case FILE_REQUEST:
	{
		add_status_line(200, ok_200_title);
		if (m_file_stat.st_size != 0)
		{
			add_headers(m_file_stat.st_size);
			m_iovec[0].iov_base = m_writeBuf;
			m_iovec[0].iov_len = m_write_idx;
			m_iovec[1].iov_base = m_file_address;
			m_iovec[1].iov_len = m_file_stat.st_size;
			m_iovec_count = 2;
			bytes_to_send = m_write_idx + m_file_stat.st_size;
			return true;
		}
		else
		{
			const char* ok_string = "<html><body></body></html>";
			add_headers(strlen(ok_string));
			if (!add_content(ok_string))
				return false;
		}
	}
	default:
		return false;
	}

	m_iovec[0].iov_base = m_writeBuf;
	m_iovec[0].iov_len = m_write_idx;
	m_iovec_count = 1;
	bytes_to_send = m_write_idx;
	return true;
}

http_conn::HTTP_CODE http_conn::do_request()
{
	char* real_file = new char[1024];
	strcpy(real_file, ROOT);
	int len = strlen(real_file);
	const char* p = strrchr(m_url, '/');

	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/");
		strcat(m_url_real, m_url + 2);
		strcpy(real_file + len, m_url_real);

		char ID[30] = { 0 };
		char password[30] = {0};
		int i;
		for (i = 3; m_content[i] != '&'; ++i)
		{
			ID[i - 3] = m_content[i];
		}
		ID[i - 1] = '\0';
		int j;
		for (j = i + 10; j < m_content_length; ++j)
		{
			password[j - 10 - i] = m_content[j];
		}
		password[j] = '\0';
		string m_ID(ID);
		string m_password(password);
		// 登录
		if (*(p + 1) == '2')
		{
			if (users.find(m_ID) != users.end() && users[m_ID] == m_password)
			{
				strcpy(m_url, "/welcome.html");
			}
			else
				strcpy(m_url, "/logError.html");
		}
		// 注册
		else
		{
			if (users.find(m_ID) != users.end())
			{
				strcpy(m_url, "/registerError.html");
			}
			else
			{
				char* sql_insert = new char[200];
				strcpy(sql_insert, "insert into user(ID, password) values('");
				strcat(sql_insert, ID);
				strcat(sql_insert, "','");
				strcat(sql_insert, password);
				strcat(sql_insert, "');");
				
				mutex_lock();
				int res = mysql_query(m_mysql, sql_insert);
				//users[m_ID] = m_password;
				users.insert(make_pair(m_ID, m_password));
				mutex_unlock();
				if (res == 0)
					strcpy(m_url, "/log.html");
				else
				{
					strcpy(m_url, "/registerError.html");
				}
				delete[] sql_insert;
			}
		}
		real_file[len + strlen(m_url_real)] = '\0';

		delete[] m_url_real;
	}
	if (*(p + 1) == '0')
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/register.html");
		strncpy(real_file + len, m_url_real, strlen(m_url_real));
		real_file[len + strlen(m_url_real)] = '\0';	// 修改了与日志字符冲突的bug

		delete[] m_url_real;
	}
	else if (*(p + 1) == '1')
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/log.html");
		strncpy(real_file + len, m_url_real, strlen(m_url_real));
		real_file[len + strlen(m_url_real)] = '\0';

		delete[] m_url_real;
	}
	else if (*(p + 1) == '5')
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/picture.html");
		strncpy(real_file + len, m_url_real, strlen(m_url_real));
		real_file[len + strlen(m_url_real)] = '\0';

		delete[] m_url_real;
	}
	else if (*(p + 1) == '6')
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/video.html");
		strncpy(real_file + len, m_url_real, strlen(m_url_real));
		real_file[len + strlen(m_url_real)] = '\0';

		delete[] m_url_real;
	}
	else if (*(p + 1) == '7')
	{
		char* m_url_real = new char[200];
		strcpy(m_url_real, "/fans.html");
		strncpy(real_file + len, m_url_real, strlen(m_url_real));
		real_file[len + strlen(m_url_real)] = '\0';

		delete[] m_url_real;
	}
	else
	{
		strcpy(real_file + len, m_url);
	}

	if (stat(real_file, &m_file_stat) < 0)
	{
		perror("stat");
		return NO_RESOURCE;
	}
	if (!(m_file_stat.st_mode & S_IROTH))
		return FORBIDDEN_REQUEST;
	if (S_ISDIR(m_file_stat.st_mode))
	{
		printf("Bad request: is dir\n");
		return BAD_REQUEST;
	}
	int fd = open(real_file, O_RDONLY);
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	return FILE_REQUEST;
}

bool http_conn::add_response(const char* format, ...)
{
	if (m_write_idx >= WRITEBUF_SIZE)
		return false;

	va_list args;
	va_start(args, format);
	int len = vsnprintf(m_writeBuf + m_write_idx, WRITEBUF_SIZE - m_write_idx - 1, format, args);
	if (len >= (WRITEBUF_SIZE - m_write_idx - 1))
	{
		va_end(args);
		return false;
	}
	m_write_idx += len;
	va_end(args);
	LOG_INFO("request: %s\n", m_writeBuf);
	Log::get_instance()->flush();
	return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_length)
{
	bool flag = false;
	flag = add_content_length(content_length);
	flag = add_linger();
	flag = add_blank_line();
	return flag;
}

bool http_conn::add_content_length(int content_length)
{
	return add_response("Content-Length:%d\r\n", content_length);
}

bool http_conn::add_linger()
{
	return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_content_type()
{
	return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_content(const char* content)
{
	return add_response("%s", content);
}

bool http_conn::add_blank_line()
{
	return add_response("%s", "\r\n");
}

bool http_conn::read()
{
	if (m_read_idx >= READBUF_SIZE)
		return false;
	int bytes_read = 0;
#ifdef connfdLT
	bytes_read = recv(m_sockfd, m_readBuf + m_read_idx, READBUF_SIZE - m_read_idx, 0);
	if (bytes_read > 0)
	{
		m_read_idx += bytes_read;
		return true;
	}
	else
	{
		perror("recv");
		return false;
	}
#endif

#ifdef connfdET
	while (1)
	{
		bytes_read = recv(m_sockfd, m_readBuf + m_read_idx, READBUF_SIZE - m_read_idx, 0);
		if (bytes_read == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break;
			}
			return false;
		}
		else if (bytes_read == 0)
		{
			return false;
		}
		m_read_idx += bytes_read;
	}
	return true;
#endif
}

void http_conn::unmap()
{
	if (m_file_address)
	{
		munmap(m_file_address, m_file_stat.st_size);
		m_file_address = 0;
	}
}

bool http_conn::write()
{
	int temp = 0;
	if (bytes_to_send == 0)
	{
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		init();
		return true;
	}

	while (1)
	{
		temp = writev(m_sockfd, m_iovec, m_iovec_count);
		if (temp < 0)
		{
			if (errno == EAGAIN)
			{
				modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			unmap();
			return false;
		}
		bytes_have_send += temp;
		bytes_to_send -= temp;
		if (bytes_have_send >= m_iovec[0].iov_len)
		{
			m_iovec[0].iov_len = 0;
			m_iovec[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
			m_iovec[1].iov_len = bytes_to_send;
		}
		else
		{
			m_iovec[0].iov_base = m_writeBuf + bytes_have_send;
			m_iovec[0].iov_len -= bytes_have_send;
		}

		if (bytes_to_send <= 0)
		{
			unmap();
			modfd(m_epollfd, m_sockfd, EPOLLIN);
			if (m_linger)
			{
				init();
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	return false;
}

void http_conn::process()
{
	HTTP_CODE read_ret = process_read();
	if (read_ret == NO_REQUEST)
	{
		printf("process_read: no request\n");
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}
	else if (read_ret == BAD_REQUEST)
	{
		printf("process_read: bad request\n");
		return;
	}
	bool write_ret = process_write(read_ret);
	if (!write_ret)
	{
		printf("process_write false!\n");
		closeconn();
	}
	modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
