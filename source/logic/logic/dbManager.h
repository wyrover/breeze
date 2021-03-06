﻿
/*
* breeze License
* Copyright (C) 2015 YaweiZhang <yawei_zhang@foxmail.com>.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


/*
*  file desc 
*  db manager
*/




#ifndef _DB_MANAGER_H_
#define _DB_MANAGER_H_
#include <common.h>





class DBManager : public Singleton<DBManager>
{
public:
	DBManager();
	~DBManager();
	//在启动连接所有需要访问的数据库.
	bool start();
	bool stop(std::function<void()> onSafeClosed);
public:
    inline void authAsyncQuery(const std::string &sql, const std::function<void(zsummer::mysql::DBResultPtr)> & handler);
    inline zsummer::mysql::DBResultPtr authQuery(const std::string &sql);

    inline void infoAsyncQuery(const std::string &sql, const std::function<void(zsummer::mysql::DBResultPtr)> & handler);
    inline zsummer::mysql::DBResultPtr infoQuery(const std::string &sql);

    inline void logAsyncQuery(const std::string &sql);
    inline zsummer::mysql::DBResultPtr logQuery(const std::string &sql);
    
private:
    static void _defaultAsyncHandler(zsummer::mysql::DBResultPtr ptr)
    {
        if (ptr->getErrorCode() != zsummer::mysql::QEC_SUCCESS)
        {
            LOGE("_defaultAsyncHandler error. msg=" << ptr->getLastError() << ", org sql=" << ptr->sqlString());
        }
    }

private:
	zsummer::mysql::DBHelperPtr _infoDB;
	zsummer::mysql::DBHelperPtr _logDB;
	zsummer::mysql::DBHelperPtr _authDB;

	zsummer::mysql::DBAsyncPtr _dbAsync;
    zsummer::mysql::DBAsyncPtr _dbLogAsync; //write log in other thread.
    
private:
    void _checkSafeClosed();
    std::function<void()> _onSafeClosed;
};


void DBManager::authAsyncQuery(const std::string &sql, const std::function<void(zsummer::mysql::DBResultPtr)> & handler)
{
    _dbAsync->asyncQuery(_authDB, sql, handler);
}
zsummer::mysql::DBResultPtr DBManager::authQuery(const std::string &sql)
{
    return _authDB->query(sql);
}

void DBManager::infoAsyncQuery(const std::string &sql, const std::function<void(zsummer::mysql::DBResultPtr)> & handler)
{
    _dbAsync->asyncQuery(_infoDB, sql, handler);
}

zsummer::mysql::DBResultPtr DBManager::infoQuery(const std::string &sql)
{
    return _infoDB->query(sql);
}


void DBManager::logAsyncQuery(const std::string &sql)
{
    _dbAsync->asyncQuery(_authDB, sql, &DBManager::_defaultAsyncHandler);
}

zsummer::mysql::DBResultPtr DBManager::logQuery(const std::string &sql)
{
    return _logDB->query(sql);
}




































#endif
