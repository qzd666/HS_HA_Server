#pragma once
#include <mysql/mysql.h>
#include <iostream>
#include <list>
#include <pthread.h>
#include <semaphore.h>
using namespace std;

class sql_conn
{
public:
	MYSQL* GetConn();
	bool RelaseConn(MYSQL* conn);
	inline int GetFreenum() { return this->m_Freenum; }
	void DestoryConn();

	static sql_conn* Get_Instance();
	void init(string url, int port, string user, string password, string database, int Maxnum);

public:
	static bool MUTEX_INIT;
	static bool SEM_INIT;
	static pthread_mutex_t init_mutex;
private:
	sql_conn();
	~sql_conn();
private:
	string m_url;
	int m_port;
	string m_user;
	string m_password;
	string m_database;
	unsigned int m_Maxnum;
	unsigned int m_Curnum;
	unsigned int m_Freenum;

	list<MYSQL*> conn_list;
	pthread_mutex_t sql_mutex;
	sem_t sql_sem;
};

class sql_connRAII
{
public:
	sql_connRAII(MYSQL** sqlRAII, sql_conn* connPool);
	~sql_connRAII();
private:
	MYSQL* SqlRAII;
	sql_conn* ConnPool;
};