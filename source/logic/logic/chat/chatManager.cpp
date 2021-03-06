﻿#include "chatManager.h"
#include "../userManager.h"
#include "../dbManager.h"
#include <ProtoCommon.h>
#include <ProtoCommonSQL.h>
#include <ProtoChatSQL.h>


ChatManager::ChatManager()
{
}

bool ChatManager::init()
{
	//! 先desc一下ContactInfo表, 不存在则创建
	auto build = ContactInfo_BUILD();
	if (DBManager::getRef().infoQuery(build[0])->getErrorCode() != QEC_SUCCESS)
	{
		if (DBManager::getRef().infoQuery(build[1])->getErrorCode() != QEC_SUCCESS)
		{
			LOGE("create table error. sql=" <<build[1]);
			return false;
		}
	}
	//! 无论ContactInfo表的字段是否有增删 都alter一遍. 
	for (size_t i = 2; i < build.size(); i++)
	{
		DBManager::getRef().infoQuery(build[i]);
	}
	

	//加载所有用户数据
	unsigned long long curID = 0;
	do
	{
		auto sql = ContactInfo_LOAD(curID);
		auto result = DBManager::getRef().infoQuery(sql);
		if (result->getErrorCode() != QEC_SUCCESS)
		{
			LOGE("load contact error. curID:" << curID << ", err=" << result->getLastError());
			return false;
		}
		if (!result->haveRow())
		{
			break;
		}
		auto mapInfo = ContactInfo_FETCH(result);
		_mapContact.insert(mapInfo.begin(), mapInfo.end());
		curID += mapInfo.size();
	} while (true);



	//监控玩家登录和退出, 用来更新玩家在线状态, 如果之后独立为一个进程后, 该通知以消息通知的形式获得.
	EventTrigger::getRef().watching(ETRIGGER_USER_LOGIN, std::bind(&ChatManager::onUserLogin, this, _1, _2, _3, _4, _5));
	EventTrigger::getRef().watching(ETRIGGER_USER_LOGOUT, std::bind(&ChatManager::onUserLogout, this, _1, _2, _3, _4, _5));
	return true;
}


void  ChatManager::onUserLogin(EventTriggerID tID, UserID uID, unsigned long long count, unsigned long long iconID, std::string nick)
{
	
	auto founder = _mapContact.find(uID);
	if (founder == _mapContact.end())
	{
		ContactInfo info;
		info.uID = uID;
		info.iconID = (short)iconID;
		info.nickName = nick;
		_mapContact[uID] = info;
		insertContact(info);
		return;
	}
	bool haveChange = false;
	if (founder->second.nickName != nick)
	{
		founder->second.nickName = nick;
		haveChange = true;
	}
	if (founder->second.iconID != iconID)
	{
		founder->second.iconID = (short)iconID;
		haveChange = true;
	}
	updateContact(founder->second, haveChange, true);
}
void  ChatManager::onUserLogout(EventTriggerID tID, UserID uID, unsigned long long, unsigned long long, std::string)
{
	auto founder = _mapContact.find(uID);
	if (founder != _mapContact.end())
	{
		founder->second.onlineFlag = false;
		updateContact(founder->second, false, true);
	}
}

void ChatManager::insertContact(const ContactInfo & info)
{
	auto sql = ContactInfo_INSERT(info);
	if (!sql.empty())
	{
		DBManager::getRef().infoAsyncQuery(sql, std::bind(&ChatManager::onUpdateContact, this, _1));
	}
}
void ChatManager::updateContact(const ContactInfo & info, bool writedb, bool broadcast)
{
	if (writedb)
	{
		auto sql = ContactInfo_UPDATE(info);
		if (!sql.empty())
		{
			DBManager::getRef().infoAsyncQuery(sql, std::bind(&ChatManager::onUpdateContact, this, _1));
		}
	}
	
	//broadcast
	if (broadcast)
	{
		WriteStream ws(ID_GetContactInfoAck);
		ws << EC_SUCCESS << info;
		for (auto &kv : info.friends)
		{
			if (kv.flag == FRIEND_ESTABLISHED)
			{
				auto ptr = UserManager::getRef().getInnerUserInfo(kv.uid);
				if (ptr && ptr->sID != InvalidSeesionID)
				{
					SessionManager::getRef().sendSessionData(ptr->sID, ws.getStream(), ws.getStreamLen());
				}
			}
		}
		auto ptr = UserManager::getRef().getInnerUserInfo(info.uID);
		if (ptr && ptr->sID != InvalidSeesionID)
		{
			SessionManager::getRef().sendSessionData(ptr->sID, ws.getStream(), ws.getStreamLen());
		}
	}
}



void ChatManager::onUpdateContact(zsummer::mysql::DBResultPtr result)
{
	if (result->getErrorCode() != QEC_SUCCESS)
	{
		LOGE("onUpdateContact error. msg=" << result->getLastError() << ", sql=" << result->sqlString());
	}
}

void ChatManager::msg_onGetContactInfoReq(TcpSessionPtr session, ProtoID pID, ReadStream & rs)
{
	GetContactInfoReq req;
	rs >> req;
	GetContactInfoAck ack;
	ack.retCode = EC_SUCCESS;
	auto founder = _mapContact.find(req.uid);
	if (founder == _mapContact.end())
	{
		ack.retCode = EC_NO_USER;
	}
	else
	{
		ack.retCode = EC_SUCCESS;
		ack.contact = founder->second;
	}
	WriteStream ws(ID_GetContactInfoAck);
	ws << ack;
	session->doSend(ws.getStream(), ws.getStreamLen());
}


void ChatManager::msg_onChatReq(TcpSessionPtr session, ProtoID pID, ReadStream & rs)
{
	ChatReq req;
	rs >> req;
	auto inner = UserManager::getRef().getInnerUserInfo(session->getSessionID());
	if (!inner)
	{
		//.
		return;
	}
	ChatInfo info;
	info.chlType = req.chlType;
	info.dstid = req.dstid;
	info.id = _genID.genNewObjID();
	info.msg = req.msg;
	info.sendTime = time(NULL);
	info.srcIcon = inner->userInfo.iconID;
	info.srcid = inner->userInfo.uID;
	info.srcName = inner->userInfo.nickName;

	if (info.chlType == CHANNEL_PRIVATE)
	{
		auto dstinner = UserManager::getRef().getInnerUserInfo(req.dstid);
		if (!dstinner)
		{
			return;
		}
		info.dstIcon = dstinner->userInfo.iconID;
		info.dstName = dstinner->userInfo.nickName;
		if (dstinner->sID != InvalidSeesionID)
		{
			WriteStream ws(ID_ChatNotice);
			ChatNotice notice;
			notice.msgs.push_back(info);
			ws << notice;
			SessionManager::getRef().sendSessionData(dstinner->sID, ws.getStream(), ws.getStreamLen());
		}
	}
	else if (info.chlType == CHANNEL_GROUP || info.chlType == CHANNEL_WORLD)
	{
// 		for ()
// 		{
// 			WriteStream ws(ID_ChatNotice);
// 			ChatNotice notice;
// 			notice.msgs.push_back(info);
// 			SessionManager::getRef().sendOrgSessionData(dstinner->sesionInfo.sID, ws.getStream(), ws.getStreamLen());
// 		}
	}
	WriteStream ws(ID_ChatAck);
	ChatAck ack;
	ack.chlType = info.chlType;
	ack.dstid = info.dstid;
	ack.msgid = info.id;
	ack.retCode = EC_SUCCESS;
	ws << ack;
	session->doSend(ws.getStream(), ws.getStreamLen());
}