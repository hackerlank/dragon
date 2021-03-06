#ifndef _SuperCommand_h_
#define _SuperCommand_h_

#include "zType.h"
#include "zNullCmd.h"
//#include "FLCommand.h"
#include <string.h>

#pragma pack(1)

namespace CMD
{

	namespace SUPER
	{
		const BYTE CMD_STARTUP	= 1;
		const BYTE CMD_GATEWAY	= 4;
		const BYTE CMD_SESSION	= 5;
		const BYTE CMD_RECORD	= 6;
		const BYTE CMD_SCENE	= 7;

		//////////////////////////////////////////////////////////////
		// 定义启动相关指令
		//////////////////////////////////////////////////////////////
		const BYTE PARA_STARTUP_REQUEST = 1;
		struct t_Startup_Request : t_NullCmd
		{
			WORD wdServerType;
			char pstrIP[MAX_IP_LENGTH];
			t_Startup_Request()
				: t_NullCmd(CMD_STARTUP, PARA_STARTUP_REQUEST) 
			{
				bzero(pstrIP, sizeof(pstrIP));
			};
		};

		const BYTE PARA_STARTUP_RESPONSE = 2;
		struct t_Startup_Response : t_NullCmd
		{
			WORD wdServerID;
			WORD wdPort;
			char pstrExtIP[MAX_IP_LENGTH];
			WORD wdExtPort;
			WORD wdNetType; // 服务器，所以在网络，0电信，1网通 
			t_Startup_Response()
				: t_NullCmd(CMD_STARTUP, PARA_STARTUP_RESPONSE) 
			{
				bzero(pstrExtIP, sizeof(pstrExtIP));
				wdServerID = 0;
				wdPort = 0;
				wdExtPort = 0;
				wdNetType = 0;
			};
		};

		struct ServerEntry
		{
			WORD wdServerID;
			WORD wdServerType;
			char pstrName[MAX_NAMESIZE];
			char pstrIP[MAX_IP_LENGTH];
			WORD wdPort;
			char pstrExtIP[MAX_IP_LENGTH];
			WORD wdExtPort;
			WORD state;
			ServerEntry()
			{
				wdServerID = 0;
				wdServerType = 0;
				bzero(pstrName, sizeof(pstrName));
				bzero(pstrIP, sizeof(pstrIP));
				wdPort = 0;
				bzero(pstrExtIP, sizeof(pstrExtIP));
				wdExtPort = 0;
				state = 0;
			}
			ServerEntry(const ServerEntry& se)
			{
				wdServerID = se.wdServerID;
				wdServerType = se.wdServerType;
				bcopy(se.pstrName, pstrName, sizeof(pstrName));
				bcopy(se.pstrIP, pstrIP, sizeof(pstrIP));
				wdPort = se.wdPort;
				bcopy(se.pstrExtIP, pstrExtIP, sizeof(pstrExtIP));
				wdExtPort = se.wdExtPort;
				state = se.state;
			}
			ServerEntry & operator= (const ServerEntry &se)
			{
				wdServerID = se.wdServerID;
				wdServerType = se.wdServerType;
				bcopy(se.pstrName, pstrName, sizeof(pstrName));
				bcopy(se.pstrIP, pstrIP, sizeof(pstrIP));
				wdPort = se.wdPort;
				bcopy(se.pstrExtIP, pstrExtIP, sizeof(pstrExtIP));
				wdExtPort = se.wdExtPort;
				state = se.state;
				return *this;
			}
		};
		const BYTE PARA_STARTUP_SERVERENTRY_NOTIFYME = 3;
		struct t_Startup_ServerEntry_NotifyMe : t_NullCmd
		{
			WORD size;
			ServerEntry entry[0];
			t_Startup_ServerEntry_NotifyMe()
				: t_NullCmd(CMD_STARTUP, PARA_STARTUP_SERVERENTRY_NOTIFYME), size(0) {};
		};
		const BYTE PARA_STARTUP_SERVERENTRY_NOTIFYOTHER = 4;
		struct t_Startup_ServerEntry_NotifyOther : t_NullCmd
		{
			WORD srcID;
			ServerEntry entry;
			t_Startup_ServerEntry_NotifyOther()
				: t_NullCmd(CMD_STARTUP, PARA_STARTUP_SERVERENTRY_NOTIFYOTHER) {};
		};

		const BYTE PARA_STARTUP_OK = 5;
		struct t_Startup_OK : t_NullCmd
		{
			WORD wdServerID;
			t_Startup_OK()
				: t_NullCmd(CMD_STARTUP, PARA_STARTUP_OK) {};
		};

		const BYTE PARA_GAMETIME = 6;
		struct t_GameTime : t_NullCmd
		{
			QWORD qwGameTime;
			QWORD qwStartTime;
			t_GameTime()
				: t_NullCmd(CMD_STARTUP, PARA_GAMETIME) {};
		};

		const BYTE PARA_RESTART_SERVERENTRY_NOTIFYOTHER = 9;
		struct t_restart_ServerEntry_NotifyOther : t_NullCmd
		{
			WORD srcID;
			WORD dstID;
			t_restart_ServerEntry_NotifyOther()
				: t_NullCmd(CMD_STARTUP, PARA_RESTART_SERVERENTRY_NOTIFYOTHER) {};
		};
		//////////////////////////////////////////////////////////////
		// 定义启动相关指令
		//////////////////////////////////////////////////////////////


		//////////////////////////////////////////////////////////////
		// 定义服务器管理器与网关服务器交互的指令
		//////////////////////////////////////////////////////////////
		const BYTE PARA_GATEWAY_GYLIST = 1;
		struct t_GYList_Gateway : t_NullCmd
		{
			WORD wdServerID;			/**< 服务器编号 */
			BYTE pstrIP[MAX_IP_LENGTH];	/**< 服务器地址 */
			WORD wdPort;				/**< 服务器端口 */
			WORD wdNumOnline;			/**< 网关在线人数 */
			int  state;					/**< 服务器状态 */
			DWORD zoneGameVersion;
			WORD wdNetType;				/**< 网关网络类型:0电信，1网通 */
			t_GYList_Gateway()
				: t_NullCmd(CMD_GATEWAY, PARA_GATEWAY_GYLIST) {};
		};

		const BYTE PARA_GATEWAY_RQGYLIST = 2;
		struct t_RQGYList_Gateway : t_NullCmd
		{
			t_RQGYList_Gateway()
				: t_NullCmd(CMD_GATEWAY, PARA_GATEWAY_RQGYLIST) {};
		};

		// 设置区ID
		const BYTE PARA_ZONE_ID_GATE = 3;
		struct t_ZoneID_Gate : t_NullCmd
		{
			GameZone_t zone;
			t_ZoneID_Gate(): t_NullCmd(CMD_GATEWAY, PARA_ZONE_ID_GATE)
			{
			}
		};

		// Super -> Gateway
		const BYTE PARA_GATE_RECONNECT_SCENE = 4;
		struct t_GateReconnectScene :t_NullCmd
		{
			ServerEntry entry;
			t_GateReconnectScene()
				:t_NullCmd(CMD_GATEWAY, PARA_GATE_RECONNECT_SCENE){}

		};

		// Super->Gateway 通知网关结束掉某个ip和端口的连接
		const BYTE PARA_GATE_TERMINATE_CONNECT = 5;
		struct t_GateTerminateConnect :t_NullCmd
		{
			char pstrIP[MAX_IP_LENGTH];
			WORD port;
			t_GateTerminateConnect()
				:t_NullCmd(CMD_GATEWAY, PARA_GATE_TERMINATE_CONNECT)
			{
				bzero(pstrIP,sizeof(pstrIP));
				port = 0;
			}
		};

		// 来自网关转发给FL的消息
		const BYTE PARA_GATE2SUPER_FORWARD_FL = 6;
		struct t_Gate2Super_Forward2FL : t_NullCmd
		{
			t_Gate2Super_Forward2FL(): t_NullCmd(CMD_GATEWAY, PARA_GATE2SUPER_FORWARD_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2GATE_FROM_FL = 7;
		struct t_Super2Gate_FromFL : t_NullCmd
		{
			t_Super2Gate_FromFL(): t_NullCmd(CMD_GATEWAY, PARA_SUPER2GATE_FROM_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_GATE2SUPER_FORWARD_ALLZONE = 8;
		struct t_Gate2Super_Forward2AllZone : t_NullCmd
		{
			t_Gate2Super_Forward2AllZone(): t_NullCmd(CMD_GATEWAY, PARA_GATE2SUPER_FORWARD_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2GATE_FROM_ALLZONE = 9;
		struct t_Super2Gate_FromAllZone : t_NullCmd
		{
			t_Super2Gate_FromAllZone(): t_NullCmd(CMD_GATEWAY, PARA_SUPER2GATE_FROM_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		//////////////////////////////////////////////////////////////
		// 定义服务器管理器与会话服务器交互的指令
		//////////////////////////////////////////////////////////////

		// 大区在线人数
		const BYTE PARA_SESSION_ONLINECOUNT = 4;
		struct t_OnlineCount_Session: t_NullCmd
		{    
			t_OnlineCount_Session(): t_NullCmd(CMD_SESSION,PARA_SESSION_ONLINECOUNT)
			{    
				onlinecount = 0;
			}
			DWORD onlinecount;
		};

		// 设置区ID
		const BYTE PARA_ZONE_ID_SESSION = 5;
		struct t_ZoneID_Session : t_NullCmd
		{
			GameZone_t zone;
			char name[MAX_NAMESIZE];
			t_ZoneID_Session(): t_NullCmd(CMD_SESSION, PARA_ZONE_ID_SESSION)
			{
				bzero(name,sizeof(name));
			}
		};

		const BYTE PARA_REQ_GM_SUPER2SESSION = 6; // 请求GM
		struct t_ReqGM_Super2Session: t_NullCmd
		{
			t_ReqGM_Super2Session(): t_NullCmd(CMD_SESSION, PARA_REQ_GM_SUPER2SESSION)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize;}
		};

		// session向super返回gm结果
		const BYTE PARA_RET_GM_RESULT_SESSION2SUPER = 7; // GM结果
		struct t_RetGM_Result_Session2Super: t_NullCmd
		{    
			t_RetGM_Result_Session2Super(): t_NullCmd(CMD_SESSION, PARA_RET_GM_RESULT_SESSION2SUPER)
			{    
				datasize = 0; 
			}    
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize;}
		};

		// super向session请求充值通知
		const BYTE PARA_PAY_NOTIFY_SUPER2SESSION = 8;
		struct t_PayNotify_Super2Session: t_NullCmd
		{
			t_PayNotify_Super2Session(): t_NullCmd(CMD_SESSION, PARA_PAY_NOTIFY_SUPER2SESSION)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize;}
		};

		const BYTE PARA_SESSION2SUPER_FORWARD_FL = 9;
		struct t_Session2Super_Forward2FL : t_NullCmd
		{
			t_Session2Super_Forward2FL(): t_NullCmd(CMD_SESSION, PARA_SESSION2SUPER_FORWARD_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2SESSION_FROM_FL = 10;
		struct t_Super2Session_FromFL : t_NullCmd
		{
			t_Super2Session_FromFL(): t_NullCmd(CMD_SESSION, PARA_SUPER2SESSION_FROM_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SESSION2SUPER_FORWARD_ALLZONE = 11;
		struct t_Session2Super_Forward2AllZone : t_NullCmd
		{
			t_Session2Super_Forward2AllZone(): t_NullCmd(CMD_SESSION, PARA_SESSION2SUPER_FORWARD_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2SESSION_FROM_ALLZONE = 12;
		struct t_Super2Session_FromAllZone : t_NullCmd
		{
			t_Super2Session_FromAllZone(): t_NullCmd(CMD_SESSION, PARA_SUPER2SESSION_FROM_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		// super向session查询充值通知
		const BYTE PARA_PAY_QUERY_SUPER2SESSION = 13;
		struct t_PayQuery_Super2Session: t_NullCmd
		{
			t_PayQuery_Super2Session(): t_NullCmd(CMD_SESSION, PARA_PAY_QUERY_SUPER2SESSION)
			{
				http_id = 0;
				datasize = 0;
			}
			DWORD http_id;
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize;}
		};

		// session向super返回充值查询结果
		const BYTE PARA_PAY_QUERY_RESULT_SESSION2SUPER = 14;
		struct t_PayQuery_Result_Session2Super: t_NullCmd
		{    
			t_PayQuery_Result_Session2Super(): t_NullCmd(CMD_SESSION, PARA_PAY_QUERY_RESULT_SESSION2SUPER)
			{
				http_id = 0;
				datasize = 0; 
			}    
			DWORD http_id;
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize;}
		};

		//////////////////////////////////////////////////////////////
		// 定义服务器管理器与场景服务器交互的指令
		//////////////////////////////////////////////////////////////

		// 设置区ID
		const BYTE PARA_ZONE_ID_SCENE = 1;
		struct t_ZoneID_Scene : t_NullCmd
		{
			GameZone_t zone;
			t_ZoneID_Scene(): t_NullCmd(CMD_SCENE, PARA_ZONE_ID_SCENE)
			{
			}
		};

		// 来自场景转发给AllZone的消息
		const BYTE PARA_SCENE2SUPER_FORWARD_ALLZONE = 2;
		struct t_Scene2Super_Forward2AllZone : t_NullCmd
		{
			t_Scene2Super_Forward2AllZone(): t_NullCmd(CMD_SCENE, PARA_SCENE2SUPER_FORWARD_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2SCENE_FROM_ALLZONE = 3;
		struct t_Super2Scene_FromAllZone : t_NullCmd
		{
			t_Super2Scene_FromAllZone(): t_NullCmd(CMD_SCENE, PARA_SUPER2SCENE_FROM_ALLZONE)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SCENE2SUPER_FORWARD_FL = 4;
		struct t_Scene2Super_Forward2FL : t_NullCmd
		{
			t_Scene2Super_Forward2FL(): t_NullCmd(CMD_SCENE, PARA_SCENE2SUPER_FORWARD_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2SCENE_FROM_FL = 5;
		struct t_Super2Scene_FromFL : t_NullCmd
		{
			t_Super2Scene_FromFL(): t_NullCmd(CMD_SCENE, PARA_SUPER2SCENE_FROM_FL)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		// Scene向Supper请求扣款通知
		const BYTE PARA_SCENE2SUPER_ASYNC_OPT_MONEY = 6;
		struct t_Scene2Super_AsyncOptMoney : t_NullCmd
		{
			t_Scene2Super_AsyncOptMoney() : t_NullCmd(CMD_SCENE, PARA_SCENE2SUPER_ASYNC_OPT_MONEY)
			{
				nOptType = 0;
				memset(szPlatform, 0, MAX_FLAT_LENGTH + 1);
				unRoleId = 0;
				unTaskId = 0;
				datasize = 0;
			}
			int nOptType;
			char szPlatform[MAX_FLAT_LENGTH+1];
			DWORD unRoleId;
			DWORD unTaskId;
			DWORD datasize;
			char data[0];

			DWORD getSize(){ return sizeof(*this) + datasize; }
		};

		// Supper向Scene返回扣款结果
		const BYTE PARA_SUPER2SCENE_ASYNC_OPT_MONEY_REPLY = 7;
		struct t_Super2Scene_AsyncOptMoneyReply : t_NullCmd
		{
			t_Super2Scene_AsyncOptMoneyReply() : t_NullCmd(CMD_SCENE, PARA_SUPER2SCENE_ASYNC_OPT_MONEY_REPLY)
			{
				nOptType = 0;
				memset(szPlatform, 0, MAX_FLAT_LENGTH + 1);
				unRoleId = 0;
				unTaskId = 0;
				datasize = 0;
			}
			int nOptType;
			char szPlatform[MAX_FLAT_LENGTH + 1];
			DWORD unRoleId;
			DWORD unTaskId;
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SCENE2SUPER_FORWARD_MATCH = 8;
		struct t_Scene2Super_Forward2Match : t_NullCmd
		{
			t_Scene2Super_Forward2Match() : t_NullCmd(CMD_SCENE, PARA_SCENE2SUPER_FORWARD_MATCH)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		const BYTE PARA_SUPER2SCENE_FROM_MATCH = 9;
		struct t_Super2Scene_FromMatch : t_NullCmd
		{
			t_Super2Scene_FromMatch() : t_NullCmd(CMD_SCENE, PARA_SUPER2SCENE_FROM_MATCH)
			{
				datasize = 0;
			}
			DWORD datasize;
			char data[0];
			DWORD getSize() const { return sizeof(*this) + datasize; }
		};

		//////////////////////////////////////////////////////////////
		// 定义服务器管理器与Record服务器交互的指令
		//////////////////////////////////////////////////////////////
		const BYTE PARA_ZONE_ID_RECORD = 3;
		struct t_ZoneID_Record : t_NullCmd
		{
			GameZone_t zone;
			t_ZoneID_Record(): t_NullCmd(CMD_RECORD, PARA_ZONE_ID_RECORD)
			{
			}
		};

	};

};

#pragma pack()

#endif

