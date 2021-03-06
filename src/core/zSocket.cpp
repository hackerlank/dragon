/**
 * \file
 * \version  $Id: zSocket.cpp 51 2013-04-16 00:37:19Z  $
 * \author  ,@163.com
 * \date 2004年11月10日 12时50分12秒 CST
 * \brief 实现类zSocket
 *
 * 
 */

#include <errno.h>
#include <unistd.h>
#include <cstdlib>
#include <arpa/inet.h>
#include <sys/types.h>
#include <string.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <zlib.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <iostream>
#include <StringUtil.h>

#include "zSocket.h"

// #define DEBUG_ZIP

/**
 * 构造函数
 * 模板偏特化
 * 动态分配内存的缓冲区，大小可以随时扩展
 */
template <>
t_BufferCmdQueue::ByteBuffer()
	: _maxSize(trunkSize), _offPtr(0), _currPtr(0), _buffer(_maxSize) { }

/**
 * 构造函数
 * 模板偏特化
 * 静态数组的缓冲区，大小不能随时改变
 */
template <>
t_StackCmdQueue::ByteBuffer()
	: _maxSize(PACKET_ZIP_BUFFER), _offPtr(0), _currPtr(0) { }

/**
 * \brief 构造函数，初始化对象
 * \param sock 套接口
 * \param addr 地址
 * \param compress 底层数据传输是否支持压缩
 */
zSocket::zSocket(
		const int sock, 
		const struct sockaddr_in *addr, 
		const bool compress)
{
	assert(-1 != sock);

	this->sock = sock;
	bzero(&this->addr, sizeof(struct sockaddr_in));
	if (NULL == addr) 
	{
		socklen_t len = sizeof(struct sockaddr);
		getpeername(this->sock, (struct sockaddr *)&this->addr, &len);
	}
	else 
	{
		bcopy(addr, &this->addr, sizeof(struct sockaddr_in));
	}
	bzero(&this->local_addr, sizeof(struct sockaddr_in));
	{
		socklen_t len = sizeof(struct sockaddr_in);
		getsockname(this->sock, (struct sockaddr *)&this->local_addr, &len);
	}

	fcntl(this->sock, F_SETFD,
			fcntl(this->sock, F_GETFD, 0)
			| FD_CLOEXEC);
	setNonblock();

	rd_msec = T_RD_MSEC;
	wr_msec = T_WR_MSEC;
	_rcv_raw_size = 0;
	_current_cmd = 0; 

	set_flag(INCOMPLETE_READ | INCOMPLETE_WRITE);
	if (compress)
		bitmask |= PACKET_ZIP;
	else
		bitmask = 0;
}

/**
 * \brief 析构函数
 */
zSocket::~zSocket()
{
	Fir::logger->debug("关闭套接口连接");
	::shutdown(sock, SHUT_RDWR);
	TEMP_FAILURE_RETRY(::close(sock));

	sock = -1;
}

#define success_unpack() \
	do { \
		if (_rcv_raw_size >= packetMinSize()/* && _rcv_queue.rd_size() >= packetMinSize()*/) \
		{ \
			unsigned int nRecordLen = packetSize(_rcv_queue.rd_buf()); \
			if (_rcv_raw_size >= nRecordLen/* && _rcv_queue.rd_size() >= nRecordLen*/) \
			{ \
				int retval = packetUnpack(_rcv_queue.rd_buf(), nRecordLen, (unsigned char *)pstrCmd); \
				_rcv_queue.rd_flip(nRecordLen); \
				_rcv_raw_size -= nRecordLen; \
				return retval; \
			} \
		} \
	} while(0)

/**
 * \brief 接收指令到缓冲区
 * \param pstrCmd 指令缓冲区
 * \param nCmdLen 指令缓冲区的大小
 * \param wait 当套接口数据没有准备好的时候，是否需要等待
 * \return 实际接收的指令大小
 * 			返回-1，表示接收错误
 * 			返回0，表示接收超时
 * 			返回整数，表示实际接收的字节数
 */
int zSocket::recvToCmd(void *pstrCmd, const int nCmdLen, const bool wait)
{
	//够一个完整的纪录，不需要向套接口接收数据
	success_unpack();

	do {
		int retval = recvToBuf();
		if (-1 == retval || (0 == retval && !wait))
			return retval;

		//够一个完整的纪录，不需要向套接口接收数据
		success_unpack();
	} while(true);

	return 0;
}

/**
 * \brief 向套接口发送原始数据，没有打包的数据，一般发送数据的时候需要加入额外的包头
 * \param pBuffer 待发送的原始数据
 * \param nSize 待发送的原始数据大小
 * \return 实际发送的字节数
 * 			返回-1，表示发送错误
 * 			返回0，表示发送超时
 * 			返回整数，表示实际发送的字节数
 */
int zSocket::sendRawData(const void *pBuffer, const int nSize)
{
	int retcode = 0;
	if (isset_flag(INCOMPLETE_WRITE))
	{
		clear_flag(INCOMPLETE_WRITE);
		goto do_select;
	}

	retcode = TEMP_FAILURE_RETRY(::send(sock, pBuffer, nSize, MSG_NOSIGNAL));
	if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
	{
do_select:
		retcode = waitForWrite();
		if (1 == retcode)
			retcode = TEMP_FAILURE_RETRY(::send(sock, pBuffer, nSize, MSG_NOSIGNAL));
		else
			return retcode;
	}

	if (retcode > 0 && retcode < nSize)
			set_flag(INCOMPLETE_WRITE);

	return retcode;
}

/**
 * \brief 发送指定字节数的原始数据，忽略超时，无限制时间的发送，直到发送完毕或者发送失败
 * \param pBuffer 待发送的原始数据
 * \param nSize 待发送的原始数据大小
 * \return 发送数据是否成功
 */
bool zSocket::sendRawDataIM(const void *pBuffer, const int nSize)
{
	if (NULL == pBuffer || nSize <= 0)
		return false;

	int offset = 0;
	do
	{
		int retcode = sendRawData(&((char *)pBuffer)[offset], nSize - offset);
		if (-1 == retcode)
		{
			return false;
		}
		offset += retcode;
	}
	while(offset < nSize);

	return (offset == nSize);
}

/**
 * \brief 发送指令
 * \param pstrCmd 待发送的内容
 * \param nCmdLen 待发送内容的大小
 * \param buffer 是否需要缓冲
 * \return 发送是否成功
 */
bool zSocket::sendCmd(const void *pstrCmd, const int nCmdLen, const bool buffer)
{
	if (NULL == pstrCmd || nCmdLen <= 0)
		return false;
	
	bool retval = true;
	if (buffer)
	{
		// 压缩后放入发送队列
		t_StackCmdQueue _raw_queue;
		packetAppendNoEnc(pstrCmd, nCmdLen, _raw_queue);
		mutex.lock();
		_snd_queue.put(_raw_queue.rd_buf(), _raw_queue.rd_size());
		_current_cmd = _raw_queue.rd_size();
		mutex.unlock();
	}
	else
	{
		// 压缩，加密，发送
		t_StackCmdQueue _raw_queue;
		packetAppend(pstrCmd, nCmdLen, _raw_queue);
		mutex.lock();
		retval = sendRawDataIM(_raw_queue.rd_buf(), _raw_queue.rd_size());
		mutex.unlock();
	}
	return retval;
}

/**
 * \brief 发送原始指令数据，不做封包动作
 * \param pstrCmd 待发送的内容
 * \param nCmdLen 待发送内容的大小
 * \param buffer 是否需要缓冲
 * \return 发送是否成功
 */
bool zSocket::sendCmdNoPack(const void *pstrCmd, const int nCmdLen, const bool buffer)
{
	if (NULL == pstrCmd || nCmdLen <= 0)
		return false;
	
	bool retval = true;
	if (buffer)
	{
		mutex.lock();
		_snd_queue.put((unsigned char *)pstrCmd, nCmdLen);
		_current_cmd = nCmdLen; 
		mutex.unlock();
	}
	else
	{
		if (need_enc())
		{
			t_StackCmdQueue _raw_queue;
			_raw_queue.put((unsigned char *)pstrCmd, nCmdLen);
			packetPackEnc(_raw_queue, _raw_queue.rd_size());
			mutex.lock();
			retval = sendRawDataIM(_raw_queue.rd_buf(), _raw_queue.rd_size());
			mutex.unlock();
		}
		else
		{
			mutex.lock();
			retval = sendRawDataIM(pstrCmd, nCmdLen);
			mutex.unlock();
		}
	}
	return retval;
}

bool zSocket::sync()
{
	if(need_enc())
	{
		unsigned int nSize = 0, current_cmd = 0, offset = 0;
		mutex.lock();
		if (_snd_queue.rd_ready())
		{
			nSize = _snd_queue.rd_size();
			current_cmd = _current_cmd;
			offset = _enc_queue.rd_size();

			_enc_queue.put(_snd_queue.rd_buf(), nSize);
			_snd_queue.rd_flip(nSize);
			_current_cmd = 0;
		}
		mutex.unlock();
		if (nSize)
			packetPackEnc(_enc_queue, current_cmd, offset);
		if (_enc_queue.rd_ready())
		{
			int retcode = sendRawData_NoPoll(_enc_queue.rd_buf(), _enc_queue.rd_size());
			if (retcode > 0)
			{
				_enc_queue.rd_flip(retcode);
			}
			else if (-1 == retcode)
			{
				return false;
			}
		}
	}
	else
	{
		mutex.lock();
		if (_snd_queue.rd_ready())
		{
			_enc_queue.put(_snd_queue.rd_buf(), _snd_queue.rd_size());
			_snd_queue.rd_flip(_snd_queue.rd_size());
		}
		mutex.unlock();
		if (_enc_queue.rd_ready())
		{
			int retcode = sendRawData_NoPoll(_enc_queue.rd_buf(), _enc_queue.rd_size());
			if (retcode > 0)
			{
				_enc_queue.rd_flip(retcode);
			}
			else if (-1 == retcode)
			{
				return false;
			}
		}
	}

	return true;
}

void zSocket::force_sync()
{
	if(need_enc())
	{
		unsigned int nSize = 0, current_cmd = 0, offset = 0;
		mutex.lock();
		if (_snd_queue.rd_ready())
		{
			nSize = _snd_queue.rd_size();
			current_cmd = _current_cmd;
			offset = _enc_queue.rd_size();
			_enc_queue.put(_snd_queue.rd_buf(), nSize);
			_snd_queue.rd_flip(nSize);
			_current_cmd = 0;
		}
		mutex.unlock();
		if (nSize)
			packetPackEnc(_enc_queue, current_cmd, offset);
		if (_enc_queue.rd_ready())
		{
			sendRawDataIM(_enc_queue.rd_buf(), _enc_queue.rd_size());
			_enc_queue.reset();
		}
	}
	else
	{
		mutex.lock();
		if (_snd_queue.rd_ready())
		{
			_enc_queue.put(_snd_queue.rd_buf(), _snd_queue.rd_size());
			_snd_queue.rd_flip(_snd_queue.rd_size());
		}
		mutex.unlock();
		if (_enc_queue.rd_ready())
		{
			sendRawDataIM(_enc_queue.rd_buf(), _enc_queue.rd_size());
			_enc_queue.reset();
		}
	}
}

/**
 * \brief 检查套接口准备好读取操作
 * \return 是否成功
 * 			-1，表示操作失败
 * 			0，表示操作超时
 * 			1，表示等待成功，套接口已经有数据准备读取
 */
int zSocket::checkIOForRead()
{
	struct pollfd pfd;

	pfd.fd = sock;
	pfd.events = POLLIN | POLLERR | POLLPRI;
	pfd.revents = 0;

	int retcode = TEMP_FAILURE_RETRY(::poll(&pfd, 1, 0));
	if (retcode > 0 && 0 == (pfd.revents & POLLIN))
		retcode = -1;

	return retcode;
}

/**
 * \brief 等待套接口准备好写入操作
 * \return 是否成功
 * 			-1，表示操作失败
 * 			0，表示操作超时
 * 			1，表示等待成功，套接口已经可以写入数据
 */
int zSocket::checkIOForWrite()
{
	struct pollfd pfd;

	pfd.fd = sock;
	pfd.events = POLLOUT | POLLERR | POLLPRI;
	pfd.revents = 0;

	int retcode = TEMP_FAILURE_RETRY(::poll(&pfd, 1, 0));
	if (retcode > 0 && 0 == (pfd.revents & POLLOUT))
		retcode = -1;

	return retcode;
}

/**
 * \brief 根据网卡名称编号获取指定网卡的IP地址
 * \param ifName 需要取地址的网卡名称
 * \return 获取的指定网卡上IP地址
 */
const char *zSocket::getIPByIfName(const char *ifName)
{
	int s;
	struct ifreq ifr;
	static const char *none_ip = "0.0.0.0";

	if (NULL == ifName)
		return none_ip;
	s = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (-1 == s) 
		return none_ip;

	bzero(ifr.ifr_name, sizeof(ifr.ifr_name));
	strncpy(ifr.ifr_name, ifName, sizeof(ifr.ifr_name) - 1);
	if (-1 == ioctl(s, SIOCGIFADDR, &ifr)) 
	{
		TEMP_FAILURE_RETRY(::close(s));
		return none_ip;
	}

	TEMP_FAILURE_RETRY(::close(s));
	return inet_ntoa(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr);
}

/**
 * \brief 	接收数据到缓冲区，保证在调用这个函数之前套接口准备好了接收，也就是使用poll轮询过
 * 			如果是加密包需要先解密到解密缓冲区
 * \return 	实际接收字节数
 * 			返回-1，表示接收错误
 * 			返回0，表示接收超时
 * 			返回整数，不加密时表示实际接收的字节数,加密时返回解密到的字节数
 */
int zSocket::recvToBuf_NoPoll()
{
	int retcode =0; 
	if(need_enc())
	{
		_rcv_queue.wr_reserve(MAX_DATABUFFERSIZE);
		retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
		if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;//should retry
		if (retcode > 0)
		{
			if ((unsigned int)retcode < _rcv_queue.wr_size()) 
			{ 
				set_flag(INCOMPLETE_READ); 
			} 
			_rcv_queue.wr_flip(retcode); 
			unsigned int size = _rcv_queue.rd_size() - _rcv_raw_size - ((_rcv_queue.rd_size() - _rcv_raw_size) % 8); 
			if(size) 
			{ 
				enc.encdec(&_rcv_queue.rd_buf()[_rcv_raw_size], size, false); 
				_rcv_raw_size += size; 
			} 
		}
	}
	else
	{
		_rcv_queue.wr_reserve(MAX_DATABUFFERSIZE);
		retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
		if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;//should retry

		if (retcode > 0)
		{
			if((unsigned int)retcode < _rcv_queue.wr_size()) 
			{ 
				set_flag(INCOMPLETE_READ); 
			} 
			_rcv_queue.wr_flip(retcode); 
			_rcv_raw_size += retcode; 
		}
	}

	if (0 == retcode) // 链接断开
		return -1;//EOF 

	return retcode;
}

int zSocket::recvToBuf_NoPoll(void *pstrCmd, const int nCmdLen)
{
	int retcode = 0;
	retcode = TEMP_FAILURE_RETRY(::recv(sock, pstrCmd, nCmdLen, MSG_NOSIGNAL));
	if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;//should retry
	if (0 == retcode) // 链接断开
		return -1;
	return retcode;
}

/**
 * \brief 接收指令到缓冲区，不从套接口接收指令，只是把接收缓冲的数据解包
 * \param pstrCmd 指令缓冲区
 * \param nCmdLen 指令缓冲区的大小
 * \return 实际接收的指令大小
 * 			返回-1，表示接收错误
 * 			返回0，表示接收超时
 * 			返回整数，表示实际接收的字节数
 */
int zSocket::recvToCmd_NoPoll(void *pstrCmd, const int nCmdLen)
{
	//够一个完整的纪录，不需要向套接口接收数据
	//success_unpack();

	//Fir::logger->info("%s,_rcv_raw_size=%u,", __PRETTY_FUNCTION__,_rcv_raw_size);
	if (_rcv_raw_size >= packetMinSize()) 
	{ 
		unsigned int nRecordLen = packetSize(_rcv_queue.rd_buf()); 
		//Fir::logger->info("%s,nRecordLen=%u,", __PRETTY_FUNCTION__,nRecordLen);
		if (_rcv_raw_size >= nRecordLen) 
		{ 
			int retval = packetUnpack(_rcv_queue.rd_buf(), nRecordLen, (unsigned char *)pstrCmd); 
			_rcv_queue.rd_flip(nRecordLen); 
			_rcv_raw_size -= nRecordLen; 
			//Fir::logger->info("%s,retval=%u,", __PRETTY_FUNCTION__,retval);
			return retval; 
		} 
	} 

	return 0;
}

/**
 * \brief 接收指令到缓冲区，不从套接口接收指令，只是把接收缓冲的数据解包
 * \param pstrCmd 指令缓冲区
 * \param nCmdLen 指令缓冲区的大小
 * \return 实际接收的指令大小
 * 			返回-1，表示接收错误
 * 			返回0，表示接收超时
 * 			返回整数，表示实际接收的字节数
 */
int zSocket::recvToHttpCmd_NoPoll(void *pstrCmd, const int nCmdLen)
{
	// Fir::logger->info("%s,_rcv_raw_size=%u,", __PRETTY_FUNCTION__,_rcv_raw_size);
	if (_rcv_raw_size >= packetHttpMinSize()) 
	{ 
		unsigned int nRecordLen = packetHttpSize((const char *)_rcv_queue.rd_buf()); 
		// Fir::logger->info("%s,nRecordLen=%u,", __PRETTY_FUNCTION__,nRecordLen);
		if (_rcv_raw_size >= nRecordLen) 
		{ 
			bcopy(_rcv_queue.rd_buf(), pstrCmd, nRecordLen);
			_rcv_queue.rd_flip(nRecordLen); 
			_rcv_raw_size -= nRecordLen; 

			return nRecordLen; 
		} 
	}

	return 0;
}
/**
 * \brief 返回整个http数据包的长度
 * \param in 数据包
 * \return 返回整个数据包的长度
 */
unsigned int zSocket::packetHttpSize(const char *in) const
{
	bool bPostType = false;

	if (memcmp(in, "GET ", 4) == 0)
	{
	}
	else if(memcmp(in, "POST ", 5) == 0)
	{
		bPostType = true;
	}
	else
	{
		Fir::logger->warn("error http cmd, %s", in);
		return -1;
	}

	// 根据http头结束符判断
	char* pHeadEnd = strstr((char*)in, "\r\n\r\n");
	if (!pHeadEnd)
		return 0;
	// Http头长度
	int nHeadLen = pHeadEnd + 4 - in;
	// Http内容长度
	int nContentLen = 0;

	if (bPostType)
	{
		char* pContentLen = strstr((char*)in, "content-length: ");
		if (!pContentLen)
		{
			pContentLen = strstr((char*)in, "Content-Length: ");
		}
		if (pContentLen)
		{
			pContentLen += strlen("content-length: ");
			char* pContentLenEnd = strstr(pContentLen, "\r\n");
			if (pContentLenEnd)
			{
				std::string strLen(pContentLen, pContentLenEnd - pContentLen);
				nContentLen = atoi(strLen.c_str());

				return nHeadLen + nContentLen;
			}
			else
			{
				Fir::logger->warn("can not find [\r\n], %s", in);
			}
		}
		else
		{
			Fir::logger->warn("can not find [content-length: ], %s", in);
		}
	}
	else
	{
		return nHeadLen + nContentLen;
	}

	return -1;
}

/**
 * \brief 向套接口发送原始数据，没有打包的数据，一般发送数据的时候需要加入额外的包头，保证在调用这个函数之前套接口准备好了接收，也就是使用poll轮询过
 * \param pBuffer 待发送的原始数据
 * \param nSize 待发送的原始数据大小
 * \return 实际发送的字节数
 * 			返回-1，表示发送错误
 * 			返回0，表示发送超时
 * 			返回整数，表示实际发送的字节数
 */
int zSocket::sendRawData_NoPoll(const void *pBuffer, const int nSize)
{
	int retcode = TEMP_FAILURE_RETRY(::send(sock, pBuffer, nSize, MSG_NOSIGNAL));
	if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;//should retry

	if (retcode > 0 && retcode < nSize)
			set_flag(INCOMPLETE_WRITE);

	return retcode;
}

/**
 * \brief 设置套接口为非阻塞模式
 * \return 操作是否成功
 */
bool zSocket::setNonblock()
{
	int fd_flags;
	int nodelay = 1;

	if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&nodelay, sizeof(nodelay)))
		return false;

	fd_flags = ::fcntl(sock, F_GETFL, 0);

#if defined(O_NONBLOCK)
	fd_flags |= O_NONBLOCK;
#elif defined(O_NDELAY)
	fd_flags |= O_NDELAY;
#elif defined(FNDELAY)
	fd_flags |= O_FNDELAY;
#else
	/* XXXX: this breaks things, but an alternative isn't obvious...*/
	return false;
#endif

	if (::fcntl(sock, F_SETFL, fd_flags) == -1) 
		return false;

	return true;
}

/**
 * \brief 等待套接口准备好读取操作
 * \return 是否成功
 * 			-1，表示操作失败
 * 			0，表示操作超时
 * 			1，表示等待成功，套接口已经有数据准备读取
 */
int zSocket::waitForRead()
{
	struct pollfd pfd;

	pfd.fd = sock;
	pfd.events = POLLIN | POLLERR | POLLPRI;
	pfd.revents = 0;

	int retcode = TEMP_FAILURE_RETRY(::poll(&pfd, 1, rd_msec));
	if (retcode > 0 && 0 == (pfd.revents & POLLIN))
		retcode = -1;

	return retcode;
}

/**
 * \brief 等待套接口准备好写入操作
 * \return 是否成功
 * 			-1，表示操作失败
 * 			0，表示操作超时 no file descriptor is selected;
 * 			1，表示等待成功，套接口已经可以写入数据
 */
int zSocket::waitForWrite()
{
	struct pollfd pfd;

	pfd.fd = sock;
	pfd.events = POLLOUT | POLLERR | POLLPRI;//POLLOUT waiting will not block
	pfd.revents = 0;

	int retcode = TEMP_FAILURE_RETRY(::poll(&pfd, 1, wr_msec));
	if (retcode > 0 && 0 == (pfd.revents & POLLOUT))
		retcode = -1;

	return retcode;
}

/**
 * \brief 	接收数据到缓冲区
 * 			如果是加密包需要解密到解密缓冲区
 * \return 	实际接收字节数
 * 			返回-1，表示接收错误
 * 			返回0，表示接收超时
 * 			返回整数，不加密包表示实际接收的字节数,加密包返回解密后实际可用的字节数
 */
int zSocket::recvToBuf()
{
	int retcode = 0;

	if(need_enc())
	{
		if (isset_flag(INCOMPLETE_READ))
		{
			clear_flag(INCOMPLETE_READ);
			goto do_select_enc;
		}

		_rcv_queue.wr_reserve(MAX_DATABUFFERSIZE);
		retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
		if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
do_select_enc:
			retcode = waitForRead();
			if (1 == retcode)
				retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
			else
				return retcode;
		}

		if (retcode > 0)
		{
			if ((unsigned int)retcode < _rcv_queue.wr_size()) 
			{ 
				set_flag(INCOMPLETE_READ); 
			} 
			_rcv_queue.wr_flip(retcode); 
			unsigned int size = _rcv_queue.rd_size() - _rcv_raw_size - ((_rcv_queue.rd_size() - _rcv_raw_size) % 8); 
			if(size) 
			{ 
				enc.encdec(&_rcv_queue.rd_buf()[_rcv_raw_size], size, false); 
				_rcv_raw_size += size; 
			} 
		}
	}
	else
	{
		if (isset_flag(INCOMPLETE_READ))
		{
			clear_flag(INCOMPLETE_READ);
			goto do_select;
		}
		_rcv_queue.wr_reserve(MAX_DATABUFFERSIZE);
		retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
		if (retcode == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
do_select:
			retcode = waitForRead();
			if (1 == retcode)
				retcode = TEMP_FAILURE_RETRY(::recv(sock, _rcv_queue.wr_buf(), _rcv_queue.wr_size(), MSG_NOSIGNAL));
			else
				return retcode;
		}

		if (retcode > 0)
		{
			if((unsigned int)retcode < _rcv_queue.wr_size()) 
			{   
				set_flag(INCOMPLETE_READ); 
			}   
			_rcv_queue.wr_flip(retcode); 
			_rcv_raw_size += retcode; 
		}
	}

	if (0 == retcode)
		return -1;//EOF 

	return retcode;
}

/**
 * \brief 			对数据进行压缩,由上层判断是否需要加密,这里只负责加密不作判断
 * \param pData 	待压缩的数据，输入
 * \param nLen 		待压缩的数据长度，输入
 * \param pBuffer 	输出，存放压缩以后的数据
 * \param _compress	当数据包过大时候是否压缩
 * \return 			返回加密以后真实数据的大小
 */
template<typename buffer_type>
unsigned int zSocket::packetPackZip(const void *pData, const unsigned int nLen, buffer_type &cmd_queue, const bool _compress)
{
	/*if (nLen > MAX_DATASIZE)
	{
		CMD::t_NullCmd *cmd = (CMD::t_NullCmd *)pData;
		Fir::logger->warn("%s: 发送的数据包过大(cmd = %u, para = %u", __FUNCTION__, cmd->cmd, cmd->para);
	}*/

	unsigned int nSize = 0;
	nSize = nLen & PACKET_MASK;
	unsigned int nMask = nLen & (~PACKET_MASK);
	if (nSize > PACKET_ZIP_MIN /*数据包过大*/ 
			&& _compress /*带压缩标记，数据包需要压缩*/
			&& !(nMask & PACKET_ZIP) /*数据包过大可能已经是压缩过的*/ )
	{
		uLong nZipLen = unzip_size(nSize);
		cmd_queue.wr_reserve(nZipLen + PH_LEN);

		int retcode = compress(&(cmd_queue.wr_buf()[PH_LEN]), &nZipLen, (const Bytef *)pData, nSize);
		switch(retcode)
		{
			case Z_OK:
				break;
			case Z_MEM_ERROR:
				Fir::logger->fatal("(%s)Z_MEM_ERROR." ,__PRETTY_FUNCTION__);
				break;
			case Z_BUF_ERROR:
				Fir::logger->fatal("(%s)Z_BUF_ERROR." ,__PRETTY_FUNCTION__);
				break;
		}
		nSize = nZipLen;
		nMask |= PACKET_ZIP;	//set packed mark
	}
	else
	{
		cmd_queue.wr_reserve(nSize + PH_LEN);
		bcopy(pData, &(cmd_queue.wr_buf()[PH_LEN]), nSize);
	}

	(*(unsigned int *)cmd_queue.wr_buf()) = (nSize | nMask);
	cmd_queue.wr_flip(nSize + PH_LEN);

	//Fir::logger->debug("%s扩展后数据包长度：%u, %u, %u", __FUNCTION__, nSize+PH_LEN, *(unsigned int *)(cmd_queue.wr_buf()) & PACKET_MASK, nLen);

	return nSize + PH_LEN;
}

/**
 * \brief 				拆包
 * \param in 			输入数据
 * \param nPacketLen 	输入时：整个数据包长度,包括PH_LEN
 * \param out 			输出数据
 * \return 				打包以后的数据包长度
 */
unsigned int zSocket::packetUnpack(unsigned char *in, const unsigned int nPacketLen, unsigned char *out)
{
	unsigned int nRecvCmdLen = nPacketLen - PH_LEN;
	//数据包压缩过
	if (PACKET_ZIP == ((*(unsigned int*)in) & PACKET_ZIP))
	{
		uLong nUnzipLen = MAX_DATASIZE;

		int retcode = uncompress(out, &nUnzipLen, &(in[PH_LEN]), nRecvCmdLen);
		switch(retcode)
		{
			case Z_OK:
				break;
			case Z_MEM_ERROR:
				{
					Fir::logger->fatal("(%s)Z_MEM_ERROR." ,__PRETTY_FUNCTION__);
					return (unsigned int)-1;
				}
				break;
			case Z_BUF_ERROR:
				{
					Fir::logger->fatal("(%s)Z_BUF_ERROR." ,__PRETTY_FUNCTION__);
					return (unsigned int)-1;
				}
				break;
			case Z_DATA_ERROR:
				{
					Fir::logger->fatal("(%s)Z_DATA_ERROR." ,__PRETTY_FUNCTION__);
					return (unsigned int)-1;
				}
				break;
		}
		//返回得到的字节数
		return nUnzipLen;
	}
	else
	{
		bcopy(&(in[PH_LEN]), out, nRecvCmdLen);
		return nRecvCmdLen;
	}
}

/**
 * \brief 对数据进行组织,需要时压缩,不加密
 * \param pData 待组织的数据，输入
 * \param nLen 待拆包的数据长度，输入
 * \param cmd_queue 输出，存放数据
 * \return 封包后的大小
 */
template<typename buffer_type>
inline unsigned int zSocket::packetAppendNoEnc(const void *pData, const unsigned int nLen, buffer_type &cmd_queue)
{
	return packetPackZip(pData, nLen, cmd_queue, PACKET_ZIP == (bitmask & PACKET_ZIP));
}

/**
 * \brief 对数据进行组织,需要时压缩和加密
 * \param pData 待组织的数据，输入
 * \param nLen 待拆包的数据长度，输入
 * \param cmd_queue 输出，存放数据
 * \return 封包后的大小
 */
template<typename buffer_type>
inline unsigned int zSocket::packetAppend(const void *pData, const unsigned int nLen, buffer_type &cmd_queue)
{
	unsigned int nSize = packetPackZip(pData, nLen, cmd_queue, PACKET_ZIP == (bitmask & PACKET_ZIP));
	if (need_enc())
	{
		nSize = packetPackEnc(cmd_queue, cmd_queue.rd_size());
	}
	return nSize;
}

/**
 * \brief 				对数据进行加密
 * \param cmd_queue		待加密的数据，输入输出
 * \param current_cmd	最后一个指令长度
 * \param offset		待加密数据的偏移
 * \return 				返回加密以后真实数据的大小
 */
template<typename buffer_type>
inline unsigned int zSocket::packetPackEnc(buffer_type &cmd_queue, const unsigned int current_cmd, const unsigned int offset)
{
	unsigned int mod = (cmd_queue.rd_size() - offset) % 8;
	if (0!=mod)
	{
		mod = 8 - mod;
		(*(unsigned int *)(&(cmd_queue.rd_buf()[cmd_queue.rd_size() - current_cmd]))) += mod;
		cmd_queue.wr_flip(mod);
	}

	//加密动作
	enc.encdec(&cmd_queue.rd_buf()[offset], cmd_queue.rd_size() - offset, true);
	
	return cmd_queue.rd_size();
}


void zProcessor::PrintDataRecv(const void * pCmd, int cmdLen)
{
	if(NULL == pCmd || cmdLen <= 0) return;
	unsigned short id = ((const unsigned short*)pCmd)[0];
	Fir::logger->debug("[消息接收], id:%d, length:%d", id, cmdLen);
	//if(cmdLen > 1000) return;
	std::string data;
	for (int i=0; i<(int)cmdLen; ++i)
	{
		std::string str = StringUtil::Format("%02X ", ((const unsigned char*)pCmd)[i]);
		data.append(str);
	}
	Fir::logger->debug("[接收数据], 内容:%s", data.c_str());
}

void zProcessor::PrintDataSend(const void *pCmd, int cmdLen)
{
	if(NULL == pCmd || cmdLen <=0) return;
	unsigned short id = ((const unsigned short*)pCmd)[0];
	Fir::logger->debug("[消息发送], id:%d, length:%d", id, cmdLen);
	//if(cmdLen > 4000) return;
	std::string data;
	for (int i=0; i<(int)cmdLen; ++i)
	{
		std::string str = StringUtil::Format("%02X ", ((const unsigned char*)pCmd)[i]);
		data.append(str);
	}
	Fir::logger->debug("[发送数据], 内容:%s", data.c_str());
}

