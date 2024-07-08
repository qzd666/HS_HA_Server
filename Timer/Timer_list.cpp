#include "Timer_list.h"

timer_list::~timer_list()
{
	t_node* tmp = head;
	while (tmp)
	{
		head = tmp->next;
		delete tmp;
		tmp = head;
	}
}
void timer_list::add_timer(t_node* TT)
{

	if (!TT)
		return;
	if (!head)
	{
		head = tail = TT;
		return;
	}

	if (TT->expire < head->expire)
	{
		TT->next = head;
		head->prev = TT;
		head = TT;
		return;
	}

	t_node* current = head;
	while (current->next && current->next->expire < TT->expire)
	{
		current = current->next;
	}

	if (!current->next)
	{
		current->next = TT;
		TT->prev = current;
		TT->next = nullptr;
		tail = TT;
	}
	else
	{
		TT->next = current->next;
		current->next->prev = TT;
		current->next = TT;
		TT->prev = current;
	}
}

void timer_list::del_timer(t_node* TT)
{
	if (!TT)
		return;
	if (TT == head && TT == tail)
	{
		delete TT;
		head = nullptr;
		tail = nullptr;
		return;
	}
	if (TT == head)
	{
		head = head->next;
		if (head)
			head->prev = nullptr;
		delete TT;
		return;
	}
	if (TT == tail)
	{
		tail = tail->prev;
		if (tail)
			tail->next = nullptr;
		delete TT;
		return;
	}
	TT->prev->next = TT->next;
	TT->next->prev = TT->prev;
	delete TT;
}

void timer_list::adjust_timer(t_node* TT)
{
	if (!TT)
		return;
	if (TT == head)
	{
		head = head->next;
		if (head)
			head->prev = nullptr;
		TT->next = nullptr;
	}
	else if (TT == tail)
	{
		return;
	}
	else
	{
		TT->prev->next = TT->next;
		TT->next->prev = TT->prev;
		TT->next = nullptr;
		TT->prev = nullptr;
	}
	add_timer(TT);
}

void timer_list::tick()
{
	if (!head)
		return;
	time_t cur = time(NULL);
	t_node* tmp = head;
	while (tmp)
	{
		if (cur < tmp->expire)
			break;
		tmp->handle();
		head = tmp->next;
		if (head)
		{
			head->prev = nullptr;
		}
		delete tmp;
		tmp = head;
	}
}
