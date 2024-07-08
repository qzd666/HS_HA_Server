#pragma once
#include "Timer.h"

class t_node;
class timer_list
{
public:
	timer_list() : head(nullptr), tail(nullptr) {}
	~timer_list();
	void add_timer(t_node* TT);
	void del_timer(t_node* TT);
	// TT�ڵ��expire��������Ҫ�ı����������λ��
	void adjust_timer(t_node* TT);
	void tick();
public:
	t_node* head;
	t_node* tail;
};