#pragma once
#include <time.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <functional>
#include "../HTTP/http_conn.h"
#include "Timer_list.h"

#define TIMEout 5

class timer_list;
class t_node;
class timer
{
public:
	timer() : prev(nullptr), next(nullptr), m_t_node(nullptr) {}

	// �����źź���
	//void addSig(int sig, void(handler)(int), bool restart = true)
	void addSig(int sig, std::function<void(int)> handler, bool restart = true);

	// �źŴ�����
	void sig_handle(int sig);

	// ��̬������װ��
	static void sig_handle_wrapper(int sig);

	// ��ʱ�������񣬷���SIGALRM
	void timer_handle();

	// ��ʱɾ���ǻ�Ծ���ӣ��ͷ���Դ
	void handle();

public:
	int m_sockfd;
	sockaddr_in addresss;
	time_t expire;
	timer* prev;
	timer* next;

	static int m_pipefd[2];	// �����źŹܵ�
	static timer_list m_timer_list;
	static int m_epollfd;

	t_node* m_t_node;
private:
	static std::function<void(int)> signalHandler_func;
};

class t_node
{
public:
	t_node() : prev(nullptr), next(nullptr) {}
	t_node(timer& t);
	std::function<void()> handle;
public:
	t_node* prev;
	t_node* next;
	time_t expire;
};
