#include "Timer.h"

function<void(int)> timer::signalHandler_func = nullptr;

void timer::addSig(int sig, std::function<void(int)> handler, bool restart)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	signalHandler_func = handler;
	sa.sa_handler = &timer::sig_handle_wrapper;
	if (restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, nullptr) != -1);
}

void timer::sig_handle(int sig)
{
	int ERRNO = errno;
	char msg = static_cast<char>(sig);
	send(m_pipefd[1], &msg, 1, 0);
	errno = ERRNO;
}

void timer::sig_handle_wrapper(int sig)
{
	if (signalHandler_func)
		signalHandler_func(sig);
}

void timer::timer_handle()
{
	m_timer_list.tick();
	alarm(TIMEout);
}

void timer::handle()
{
	epoll_ctl(m_epollfd, EPOLL_CTL_DEL, m_sockfd, 0);
	close(m_sockfd);
	http_conn::userNum--;
}

t_node::t_node(timer& t)
{
	expire = t.expire;
	prev = nullptr;
	next = nullptr;
	handle = std::bind(&timer::handle, t);
}
