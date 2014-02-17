#pragma once

#include "IBuffPacket.h"
#include "ClientCommand.h"
#include "IObject.h"
#include "UserValidManager.h"
#include "UserInfoManager.h"

#include <string>
#include <map>

//定义客户端信令(TCP)
#define COMMAND_LOGIN                  0x2100     //登陆
#define COMMAND_LOGOUT                 0x2101     //退出 
#define COMMAND_USERINFO               0x2102     //查询用户信息 
#define COMMAND_SET_USERINFO           0x2103     //设置用户信息 
#define COMMAND_RETURN_LOGIN           0xe100     //登陆应答 
#define COMMAND_RETURN_LOGOUT          0xe101     //登出应答
#define COMMAND_RETURN_USERINFO        0xe102     //查询用户信息
#define COMMAND_RETURN_SET_USERINFO    0xe103     //设置用户信息 

//中间服务器通讯信令(TCP)
#define SERVER_COMMAND_USERVALID   0xc001    //到数据源查询UserValid请求
#define SERVER_COMMAND_USERINFO    0xc002    //到数据源查询UserInfo请求

#define SERVER_COMMAND_USERVALID_R 0xc101    //应答查询UserValid请求
#define SERVER_COMMAND_USERINFO_R  0xc102    //应答查询UserInfo请求

//定义常用变量
#define LOGIN_SUCCESS            0
#define LOGIN_FAIL_NOEXIST       1
#define LOGIN_FAIL_ONLINE        2
#define LOGIN_FAIL_PASSWORD      3

#define OP_OK                    0
#define OP_FAIL                  1

using namespace std;

typedef ACE_Singleton<CUserValidManager, ACE_Null_Mutex> App_UserValidManager;
typedef ACE_Singleton<CUserInfoManager, ACE_Null_Mutex> App_UserInfoManager;

class CPostServerData : public IClientMessage
{
public:
	CPostServerData() 
	{ 
		m_pServerObject = NULL;
	};

	~CPostServerData() {};

	bool RecvData(ACE_Message_Block* mbRecv)
	{
		//判断返回数据块是否小于0
		uint32 u4SendPacketSize = (uint32)mbRecv->length();
		if(u4SendPacketSize <= 0)
		{
			OUR_DEBUG((LM_INFO, "[CPostServerData::RecvData]Get Data(%d).\n", u4SendPacketSize));
		}

		OUR_DEBUG((LM_INFO, "[CPostServerData::RecvData]Get Data(%d).\n", u4SendPacketSize));
		if(NULL != m_pServerObject)
		{
			//解析获得的数据
			char* pData = new char[u4SendPacketSize];
			ACE_OS::memcpy(pData, mbRecv->rd_ptr(), u4SendPacketSize);

			uint16 u2WatchCommand = 0;

			//解析获得的数据块
			//4字节包长+2字节名称长度+名称+ConnectID
			uint32 u4PacketSize = 0;
			int    nPos = 0;
			ACE_OS::memcpy(&u4PacketSize, (char* )&pData[nPos], sizeof(uint32));
			nPos += sizeof(uint32);

			//先解析出命令ID
			ACE_OS::memcpy(&u2WatchCommand, (char* )&pData[nPos], sizeof(uint16));
			nPos += sizeof(uint16);

			if(u2WatchCommand == SERVER_COMMAND_USERVALID_R)
			{
				uint16 u2UserName = 0;
				ACE_OS::memcpy(&u2UserName, (char* )&pData[nPos], sizeof(uint16));
				nPos += sizeof(uint16);
				char* pUserName = new char[u2UserName + 1];
				ACE_OS::memset(pUserName, 0, u2UserName + 1);
				ACE_OS::memcpy(pUserName, (char* )&pData[nPos], u2UserName);
				nPos += u2UserName;
				uint16 u2UserPass = 0;
				ACE_OS::memcpy(&u2UserPass, (char* )&pData[nPos], sizeof(uint16));
				nPos += sizeof(uint16);
				char* pUserPass = new char[u2UserPass + 1];
				ACE_OS::memset(pUserPass, 0, u2UserPass + 1);
				ACE_OS::memcpy(pUserPass, (char* )&pData[nPos], u2UserPass);
				nPos += u2UserPass;
				uint8 u1Ret = 0;
				ACE_OS::memcpy(&u1Ret, (char* )&pData[nPos], sizeof(uint8));
				nPos += sizeof(uint8);
				uint32 u4CacheIndex = 0;
				ACE_OS::memcpy(&u4CacheIndex, (char* )&pData[nPos], sizeof(uint32));
				nPos += sizeof(uint32);
				uint32 u4ConnectID = 0;
				ACE_OS::memcpy(&u4ConnectID, (char* )&pData[nPos], sizeof(uint32));
				nPos += sizeof(uint32);

				if(u4CacheIndex == 0)
				{
					//重新加载一下缓冲
					App_UserValidManager::instance()->GetFreeValid();
				}
				else
				{
					//Lru被触发，需要重新遍历一下内存
					App_UserValidManager::instance()->Reload_Map_CacheMemory(u4CacheIndex);
				}


				uint32 u4Ret = LOGIN_SUCCESS;
				//在重新找一下
				_UserValid* pUserValid = App_UserValidManager::instance()->GetUserValid(pUserName);
				if(NULL != pUserValid)
				{
					//比较用户密码是否正确
					if(ACE_OS::strcmp(pUserValid->m_szUserPass, pUserPass) == 0)
					{
						pUserValid->m_u4LoginCount++;
						u4Ret = LOGIN_SUCCESS;
					}
					else
					{
						u4Ret = LOGIN_FAIL_PASSWORD;
					}
				}
				else
				{
					u4Ret = LOGIN_FAIL_NOEXIST;
				}

				App_UserValidManager::instance()->Display();

				SAFE_DELETE_ARRAY(pUserPass);
				SAFE_DELETE_ARRAY(pUserName);
				
				IBuffPacket* pResponsesPacket = m_pServerObject->GetPacketManager()->Create();
				uint16 u2PostCommandID = COMMAND_RETURN_LOGIN;

				//返回验证结果
				(*pResponsesPacket) << (uint16)u2PostCommandID;   //拼接应答命令ID
				(*pResponsesPacket) << (uint32)u4Ret;

				if(NULL != m_pServerObject->GetConnectManager())
				{
					//发送全部数据
					m_pServerObject->GetConnectManager()->PostMessage(u4ConnectID, pResponsesPacket, SENDMESSAGE_NOMAL, u2PostCommandID, true);
				}
				else
				{
					OUR_DEBUG((LM_INFO, "[CBaseCommand::DoMessage] m_pConnectManager = NULL"));
					m_pServerObject->GetPacketManager()->Delete(pResponsesPacket);
				}
			}
			else if(u2WatchCommand == SERVER_COMMAND_USERINFO_R)
			{
				uint32 u4Ret        = 0;
				uint32 u4UserID     = 0;
				uint32 u4CacheIndex = 0;
				uint32 u4ConnectID  = 0;

				ACE_OS::memcpy(&u4Ret, (char* )&pData[nPos], sizeof(uint8));
				nPos += sizeof(uint8);
				ACE_OS::memcpy(&u4UserID, (char* )&pData[nPos], sizeof(uint32));
				nPos += sizeof(uint32);
				ACE_OS::memcpy(&u4CacheIndex, (char* )&pData[nPos], sizeof(uint32));
				nPos += sizeof(uint32);
				ACE_OS::memcpy(&u4ConnectID, (char* )&pData[nPos], sizeof(uint32));
				nPos += sizeof(uint32);

				if(u4CacheIndex == 0)
				{
					//重新加载一下缓冲
					App_UserInfoManager::instance()->GetFreeUserInfo();
				}
				else
				{
					//Lru被触发，需要重新遍历一下内存
					App_UserInfoManager::instance()->Reload_Map_CacheMemory(u4CacheIndex);
				}

				IBuffPacket* pResponsesPacket = m_pServerObject->GetPacketManager()->Create();
				uint16 u2PostCommandID = SERVER_COMMAND_USERINFO_R;
				u4Ret = LOGIN_SUCCESS;
				//在重新找一下
				_UserInfo* pUserInfo = App_UserInfoManager::instance()->GetUserInfo(u4UserID);
				if(pUserInfo != NULL)
				{
					u4Ret = LOGIN_SUCCESS;
					(*pResponsesPacket) << (uint16)u2PostCommandID;   //拼接应答命令ID
					(*pResponsesPacket) << (uint32)u4Ret;
					(*pResponsesPacket) << (uint32)pUserInfo->m_u4UserID;
					(*pResponsesPacket) << (uint32)pUserInfo->m_u4Life;
					(*pResponsesPacket) << (uint32)pUserInfo->m_u4Magic;
				}
				else
				{
					u4Ret = LOGIN_FAIL_NOEXIST;
					(*pResponsesPacket) << (uint16)u2PostCommandID;   //拼接应答命令ID
					(*pResponsesPacket) << (uint32)u4Ret;
					(*pResponsesPacket) << (uint32)0;
					(*pResponsesPacket) << (uint32)0;
					(*pResponsesPacket) << (uint32)0;
				}

				if(NULL != m_pServerObject->GetConnectManager())
				{
					//发送全部数据
					m_pServerObject->GetConnectManager()->PostMessage(u4ConnectID, pResponsesPacket, SENDMESSAGE_NOMAL, u2PostCommandID, true);
				}
				else
				{
					OUR_DEBUG((LM_INFO, "[CBaseCommand::DoMessage] m_pConnectManager = NULL"));
					m_pServerObject->GetPacketManager()->Delete(pResponsesPacket);
				}
			}

			SAFE_DELETE_ARRAY(pData);
		}

		return true;
	};

	bool ConnectError(int nError)
	{
		OUR_DEBUG((LM_ERROR, "[CPostServerData::ConnectError]Get Error(%d).\n", nError));
		return true;
	};

	void SetServerObject(CServerObject* pServerObject)
	{
		m_pServerObject = pServerObject;
	}

private:
	CServerObject* m_pServerObject;
};

class CBaseCommand : public CClientCommand
{
public:
  CBaseCommand(void);
  ~CBaseCommand(void);

  void InitUserList();
  void ClearUserList();
  int DoMessage(IMessage* pMessage, bool& bDeleteFlag);
  void SetServerObject(CServerObject* pServerObject);

private:
	void Do_User_Login(IMessage* pMessage);        //处理登陆
	void Do_User_Logout(IMessage* pMessage);       //处理登出
	void Do_User_Info(IMessage* pMessage);         //获得用户信息
	void Do_Set_User_Info(IMessage* pMessage);     //设置用户信息 

private:
  CServerObject*    m_pServerObject;
  CPostServerData*  m_pPostServerData;
};


