#include <iostream>
#include <assert.h>
#include <functional>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "threadpool.h"
#include "Timer.h"
#include "Timer_list.h"
#include "mysql_conn.h"
#include "http_conn.h"
#include "Log.h"
using namespace std;

#define SYNLOG
//#define ASYNLOG

#define listenFdLT
//#define listenFdET
#define MAX_FD 10000
#define THREAD_NUM 10
#define Worker_MAXNUM 1000
#define Events_MAXNUM 10000
#define TIMEOUT 5

extern void addfd(int epollfd, int sockfd, bool one_shot);
extern void modfd(int epollfd, int sockfd, int ev);
extern void removefd(int epollfd, int sockfd);
extern int setnonblocking(int fd);

static int epollFd = 0;

timer_list timer::m_timer_list;
int timer::m_pipefd[2];
int timer::m_epollfd;

int main(int argc, char* argv[])
{
#ifdef SYNLOG
	Log::get_instance()->init("ServerLog", 2000, 800000, 0);
#endif

#ifdef ASYNLOG
	Log::get_instance()->init("ServerLog", 2000, 800000, 8);
#endif

	if (argc <= 1)
	{
		printf("usage: %s port\n", basename(argv[0]));
		return 1;
	}
	int port = atoi(argv[1]);
	timer TMP_Timer;	// 临时对象，用于捆绑信号函数
	TMP_Timer.addSig(SIGPIPE, SIG_IGN);

	sql_conn* Sql_Conn = sql_conn::Get_Instance();
	Sql_Conn->init("localhost", 3306, "root", "123456", "web1", 10);
	http_conn* Http_Conn = new http_conn[MAX_FD];
	assert(Http_Conn);
	for (int i = 0; i < MAX_FD; ++i)
	{
		Http_Conn[i].sql_result_init(Sql_Conn);
	}

	Threadpool<http_conn> *ThreadPool = nullptr;
	ThreadPool = new Threadpool<http_conn>(Sql_Conn, THREAD_NUM, Worker_MAXNUM);
	assert(ThreadPool);

	int listenFd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenFd >= 0);

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);

	int ret = 0;
	int flag = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
	ret = bind(listenFd, (struct sockaddr*)&address, sizeof(address));
	assert(ret >= 0);
	ret = listen(listenFd, 5);
	assert(ret >= 0);

	epoll_event events[Events_MAXNUM];
	epollFd = epoll_create(6);	// 大于0即可，该参数已失去意义
	timer::m_epollfd = epollFd;
	assert(epollFd != -1);
	
	addfd(epollFd, listenFd, false);
	http_conn::m_epollfd = epollFd;

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, timer::m_pipefd);
	assert(ret != -1);
	setnonblocking(timer::m_pipefd[1]);
	addfd(epollFd, timer::m_pipefd[0], false);
	TMP_Timer.addSig(SIGALRM, bind(&timer::sig_handle, &TMP_Timer, std::placeholders::_1), false);
	TMP_Timer.addSig(SIGTERM, bind(&timer::sig_handle, &TMP_Timer, std::placeholders::_1), false);

	timer* Timer = new timer[MAX_FD];

	bool server_stop = false;
	bool timeout = false;
	alarm(TIMEOUT);

	while (!server_stop)
	{
		int Num = epoll_wait(epollFd, events, Events_MAXNUM, -1);
		if (Num < 0 && errno != EINTR)
		{
			//LOG_ERROR("%s", "epoll_wait fail");
			break;
		}

		for (int i = 0; i < Num; ++i)
		{
			int sockFd = events[i].data.fd;
			if (sockFd == listenFd)
			{
				struct sockaddr_in client_addr;
				socklen_t client_addrLen = sizeof(client_addr);
#ifdef listenFdLT
				int connFd = accept(listenFd, (struct sockaddr*)&client_addr, &client_addrLen);
				if (connFd < 0)
				{
					//LOG_ERROR("%s, erron is: %d", "accept error", errno);
					continue;
				}
				if (http_conn::userNum >= MAX_FD)
				{
					perror("Internal server busy");
					//LOG_ERROR("%s", "Internal server busy");
					continue;
				}
				Http_Conn[connFd].init(connFd, client_addr);

				// 初始化定时器
				Timer[connFd].m_sockfd = connFd;
				Timer[connFd].addresss = client_addr;
				time_t cur = time(nullptr);
				Timer[connFd].expire = cur + 3 * TIMEOUT;

				t_node* T_node = new t_node(Timer[connFd]);
				Timer[connFd].m_t_node = T_node;
				timer::m_timer_list.add_timer(T_node);

#endif

#ifdef listenFdET
				while (1)
				{
					int connFd = accept(listenFd, (struct sockaddr*)&client_addr, &client_addrLen);
					if (connFd < 0)
					{
						break;
					}
					if (http_conn::userNum >= MAX_FD)
					{
						perror("Internal server busy");
						break;
					}
					Http_Conn[connFd].init(connFd, client_addr);

					// 初始化定时器
					Timer[connFd].m_sockfd = connFd;
					Timer[connFd].addresss = client_addr;
					time_t cur = time(nullptr);
					Timer[connFd].expire = cur + 3 * TIMEOUT;

					t_node* T_node = new t_node(Timer[connFd]);
					Timer[connFd].m_t_node = T_node;
					timer::m_timer_list.add_timer(T_node);
				}
				continue;
#endif
			}
			else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
			{
				// 服务端关闭连接
				Timer[sockFd].handle();
				//Http_Conn[sockFd].closeconn();

				t_node* T_node = Timer[sockFd].m_t_node;
				timer::m_timer_list.del_timer(T_node);
			}
			else if ((sockFd == timer::m_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				char signals[1024];
				ret = recv(timer::m_pipefd[0], signals, sizeof(signals), 0);
				if (ret == -1 || ret == 0)
				{
					continue;
				}
				else
				{
					for (int i = 0; i < ret; ++i)
					{
						switch (signals[i])
						{
						case SIGALRM:
						{
							timeout = true;
							break;
						}
						case SIGTERM:
						{
							server_stop = true;
						}
						}
					}
				}
			}
			else if (events[i].events & EPOLLIN)
			{
				if (Http_Conn[sockFd].read())
				{
					LOG_INFO("deal with the client(%s)", inet_ntoa(Http_Conn[sockFd].get_address()->sin_addr));
					Log::get_instance()->flush();
					ThreadPool->append(Http_Conn + sockFd);
					
					time_t cur = time(nullptr);
					Timer[sockFd].expire = cur + 3 * TIMEOUT;

					t_node* T_node = Timer[sockFd].m_t_node;
					timer::m_timer_list.adjust_timer(T_node);
					LOG_INFO("%s", "adjust timer once");
					Log::get_instance()->flush();
				}
				else
				{
					printf("read false!!!\n");
					Timer[sockFd].handle();

					t_node* T_node = Timer[sockFd].m_t_node;
					timer::m_timer_list.del_timer(T_node);
					//Http_Conn[sockFd].closeconn();
				}
			}
			else if (events[i].events & EPOLLOUT)
			{
				if (Http_Conn[sockFd].write())
				{
					LOG_INFO("send data to the client(%s)", inet_ntoa(Http_Conn[sockFd].get_address()->sin_addr));
					Log::get_instance()->flush();
					time_t cur = time(nullptr);
					Timer[sockFd].expire = cur + 3 * TIMEOUT;

					t_node* T_node = Timer[sockFd].m_t_node;
					timer::m_timer_list.adjust_timer(T_node);
					LOG_INFO("%s", "adjust timer once");
					Log::get_instance()->flush();
				}
				else
				{
					Timer[sockFd].handle();

					t_node* T_node = Timer[sockFd].m_t_node;
					timer::m_timer_list.del_timer(T_node);
					//Http_Conn[sockFd].closeconn();
				}
			}
		}
		if (timeout)
		{
			timer::m_timer_list.tick();	// 将定时器链表的超时timer剔除
			alarm(TIMEOUT);
			timeout = false;
		}
	}
	close(epollFd);
	close(listenFd);
	delete[] Http_Conn;
	delete[] Timer;
	delete ThreadPool;
	Sql_Conn->DestoryConn();

	return 0;
}