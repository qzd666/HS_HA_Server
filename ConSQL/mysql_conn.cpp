#include "mysql_conn.h"
#include <semaphore.h>
#include <pthread.h>

bool sql_conn::MUTEX_INIT = false;
bool sql_conn::SEM_INIT = false;
pthread_mutex_t sql_conn::init_mutex = PTHREAD_MUTEX_INITIALIZER;
sql_conn::sql_conn()
{
	m_Curnum = 0;
	m_Freenum = 0;
}

sql_conn::~sql_conn()
{
	DestoryConn();
}

sql_conn* sql_conn::Get_Instance()
{
	static sql_conn Sql_conn;
	return &Sql_conn;
}

void sql_conn::init(string url, int port, string user, string password, string database, int Maxnum)
{
	m_url = url;
	m_port = port;
	m_user = user;
	m_password = password;
	m_database = database;
	if (!sql_conn::MUTEX_INIT)
	{
		pthread_mutex_lock(&init_mutex);
		if (pthread_mutex_init(&sql_mutex, 0) != 0)
		{
			perror("pthread_mutex_init");
			exit(1);
		}
		sql_conn::MUTEX_INIT = true;
		pthread_mutex_unlock(&init_mutex);
	}
	pthread_mutex_lock(&sql_mutex);
	for (int i = 0; i < Maxnum; ++i)
	{
		MYSQL* conn = nullptr;
		conn = mysql_init(conn);
		if (conn == nullptr)
		{
			cout << "Error: " << mysql_error(conn) << endl;
			exit(1);
		}
		conn = mysql_real_connect(conn, m_url.c_str(), m_user.c_str(), m_password.c_str(), m_database.c_str(), port, NULL, 0);
		if (conn == nullptr)
		{
			cout << "Error: " << mysql_error(conn) << endl;
			exit(1);
		}
		conn_list.push_back(conn);
		m_Freenum++;
	}
	if (!sql_conn::SEM_INIT)
	{
		pthread_mutex_lock(&init_mutex);
		if (sem_init(&sql_sem, 0, m_Freenum) != 0)
		{
			perror("sem_init");
			exit(1);
		}
		sql_conn::SEM_INIT = true;
		pthread_mutex_unlock(&init_mutex);
	}
	m_Maxnum = m_Freenum;
	pthread_mutex_unlock(&sql_mutex);
}

MYSQL* sql_conn::GetConn()
{
	if (conn_list.size() == 0)
		return nullptr;
	MYSQL* conn = nullptr;
	sem_wait(&sql_sem);

	pthread_mutex_lock(&sql_mutex);
	conn = conn_list.front();
	conn_list.pop_front();
	m_Freenum--;
	m_Curnum++;
	pthread_mutex_unlock(&sql_mutex);
	return conn;
}

bool sql_conn::RelaseConn(MYSQL* conn)
{
	if (conn == nullptr)
	{
		return false;
	}
	pthread_mutex_lock(&sql_mutex);
	conn_list.push_back(conn);
	m_Freenum++;
	m_Curnum--;
	pthread_mutex_unlock(&sql_mutex);

	sem_post(&sql_sem);
	return true;
}

void sql_conn::DestoryConn()
{
	pthread_mutex_lock(&sql_mutex);
	if (conn_list.size() > 0)
	{
		list<MYSQL*>::iterator it;
		for (it = conn_list.begin(); it != conn_list.end(); ++it)
		{
			MYSQL* conn = *it;
			mysql_close(conn);
		}
		m_Freenum = 0;
		m_Curnum = 0;
		m_Maxnum = 0;
		conn_list.clear();
	}
	pthread_mutex_unlock(&sql_mutex);

}

sql_connRAII::sql_connRAII(MYSQL** sqlRAII, sql_conn* connPool)
{
	*sqlRAII = connPool->GetConn();

	SqlRAII = *sqlRAII;
	ConnPool = connPool;
}

sql_connRAII::~sql_connRAII()
{
	ConnPool->RelaseConn(SqlRAII);
}
