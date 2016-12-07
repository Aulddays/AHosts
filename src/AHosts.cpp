// AHosts.cpp : Defines the AHosts class
//

#include "stdafx.h"
#include <boost/bind.hpp>
#include "AHosts.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

boost::mt19937 AHostsRand((uint32_t)time(NULL) + _getpid());

// AHosts
//AHosts::AHosts()
//{
//}
//
//
//AHosts::~AHosts()
//{
//}

int AHosts::start()
{
	// load conf
	if (int res = m_conf.load())
		PELOG_ERROR_RETURN((PLV_ERROR, "Load conf failed.\n"), res);

	// start receive
	asio::error_code ec;
	asio::socket_base::reuse_address option(true);
	m_uSocket.set_option(option);
	if (m_uSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 8453), ec))
		PELOG_ERROR_RETURN((PLV_ERROR, "UDP bind failed: %s\n", ec.message().c_str()), ec.value());
	int res = 0;
	if (res = listenUdp())
		PELOG_ERROR_RETURN((PLV_ERROR, "AHosts start failed.\n"), res);
	m_hbTimer.expires_from_now(boost::posix_time::milliseconds(HBTIMEMS));
	m_hbTimer.async_wait(boost::bind(&AHosts::onHeartbeat, this, asio::placeholders::error));

	return m_ioService.run();
}

int AHosts::listenUdp()
{
	m_uRemote = asio::ip::udp::endpoint();	// clear remote
	m_ucRecvBuf.resize(520);
	m_uSocket.async_receive_from(asio::buffer(m_ucRecvBuf, m_ucRecvBuf.size()), m_uRemote,
		boost::bind(&AHosts::onUdpRequest, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void AHosts::onUdpRequest(const asio::error_code& error, size_t size)
{
	if (error)
		PELOG_LOG((PLV_ERROR, "Receive request failed. %s\n", error.message().c_str()));
	else
	{
		PELOG_LOG((PLV_VERBOSE, "Got request from %s:%d size " PL_SIZET "\n",
			m_uRemote.address().to_string().c_str(), (int)m_uRemote.port(), size));
		m_ucRecvBuf.resize(size);
		AHostsJob *job = new AHostsJob(this, m_ioService);
		m_jobs.insert(job);
		job->runudp(m_uSocket, m_uRemote, m_ucRecvBuf);
	}
	if (listenUdp())
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "UDP listening failed.\n"));
}

int AHosts::jobComplete(AHostsJob *job)
{
	if (m_jobs.find(job) == m_jobs.end())
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid job ptr.\n"), 1);
	PELOG_LOG((PLV_TRACE, "Job complete\n"));
	job->finished();
	m_jobs.erase(job);
	delete job;
	return 0;
}

void AHosts::onHeartbeat(const asio::error_code& error)
{
	//PELOG_LOG((PLV_DEBUG, "Heart Beat\n"));
	if (!error)
	{
		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		for (auto i = m_jobs.begin(); i != m_jobs.end(); ++i)
			(*i)->heartBeat(now);
		m_hbTimer.expires_from_now(boost::posix_time::milliseconds(HBTIMEMS));
		m_hbTimer.async_wait(boost::bind(&AHosts::onHeartbeat, this, asio::placeholders::error));
	}
	else if (error)
	{
		PELOG_LOG((PLV_ERROR, "Heartbeat invalid state %s\n", error.message().c_str()));
	}
}


// AHostsJob

AHostsJob::AHostsJob(AHosts *ahosts, asio::io_service &ioService)
	: m_ahosts(ahosts), m_ioService(ioService), m_client(NULL), m_status(JOB_BEGIN),
	m_jobst(JOB_OK), m_questionNum(-1)
{
	//m_server = new UdpServer(this, m_ioService);
	m_tstart = m_tearly = m_tanswer = m_treply = m_tfin = boost::posix_time::microsec_clock::local_time();
	//m_server->setClient(m_client);
}

int AHostsJob::runudp(const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote, const abuf<char> &req)
{
	m_client = new UdpClient(this, m_ioService);
	if (int ret = ((UdpClient *)m_client)->run(req, socket, remote))
	{
		m_jobst = JOB_REQERROR;
		m_ahosts->jobComplete(this);
		return ret;
	}
	return 0;
}

int AHostsJob::request(const aulddays::abuf<char> &req)
{
	//PELOG_LOG((PLV_DEBUG, "Dump request(" PL_SIZET "):\n", req.size()));
	//dumpMessage(req);
	// decompress message
	abuf<char> dcompreq;
	if (codecMessage(false, req, dcompreq))
	{
		// decompress failed, bypass the cache and handler, but still try recursive
		PELOG_LOG((PLV_WARNING, "Decompress request failed. bypass to server.\n"));
		m_request.scopyFrom(req);
		m_jobst = JOB_REQERROR;
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Decompressed request(" PL_SIZET "):\n", dcompreq.size()));
		dumpMessage(dcompreq, false);

		// get request and check cache
		m_questionNum = getNameType(dcompreq, m_reqNameType);
		nametype2print(m_reqNameType, m_reqPrint);
		if (m_questionNum == 1)	// exactly one question record, check the cache
		{
			time_t uptime;
			time_t now = time(NULL);
			int32_t ttl = 0;
			if(m_ahosts->m_cache.get(m_reqNameType, m_reqNameType.size(), m_cached, uptime, ttl) == 0)
			{
				PELOG_LOG((PLV_VERBOSE, "Got answer from cache. %s\n", m_reqPrint.buf()));
				int res = manageTtl(m_cached, uptime, now);
				if (uptime != now && res >= 0)	// should update ttl in cache
				{
					time_t timediff = now > uptime ? now - uptime : uptime - now;
					ttl = timediff >= ttl ? 0 : (int32_t)(ttl - timediff);
					m_ahosts->m_cache.set(m_reqNameType, m_reqNameType.size(), m_cached.buf(), m_cached.size(), ttl);
				}
				if (res >= 0 && ttl > 0)	// cache valid, send back directly without recursing
				{
					PELOG_LOG((PLV_VERBOSE, "Cache valid. %s TTL %d\n", m_reqPrint.buf(), (int)ttl));
					return serverComplete(NULL, m_cached);
				}
				else
					PELOG_LOG((PLV_VERBOSE, "Cache expired. %s\n", m_reqPrint.buf()));
			}
		}

		// TODO: call handler

		// recompress message after handler
		codecMessage(true, dcompreq, m_request);
		//PELOG_LOG((PLV_DEBUG, "Recompressed request(" PL_SIZET "):\n", m_request.size()));
		//dumpMessage(m_request);
	}
	// send to upstream servers
	m_status = JOB_REQUESTING;
	const static asio::ip::udp::endpoint serverconf[] = {
		asio::ip::udp::endpoint(asio::ip::address::from_string("223.5.5.5"), 53),
		//asio::ip::udp::endpoint(asio::ip::address::from_string("208.67.220.220"), 53),
		//asio::ip::udp::endpoint(asio::ip::address::from_string("8.8.8.8"), 53),
	};
	for (size_t i = 0; i < sizeof(serverconf) / sizeof(serverconf[0]); ++i)
	{
		UdpServer *server = new UdpServer(this, m_ioService, serverconf[i], 20000);
		m_server.insert(server);
		server->send(m_request);
	}
	return 0;
}

int AHostsJob::clientComplete(DnsClient *client, int status)
{
	if (client != m_client)
	{
		PELOG_LOG((PLV_ERROR, "Invalid client ptr\n"));
		status = -1;
	}
	if (m_status != JOB_GOTANSWER && m_status != JOB_EARLYRET)
	{
		PELOG_LOG((PLV_ERROR, "Invalid job status %d\n", m_status));
		m_status = JOB_GOTANSWER;
		status = -1;
	}
	if (status <= 0)
		m_treply = boost::posix_time::microsec_clock::local_time();
	// on early return, we do not change m_status, but serverComplete() has to
	// check m_client()->responded() to determine client has finished
	if (m_status != JOB_EARLYRET || m_server.size() == 0)
		m_status = JOB_RESPONDED;
	if (status < 0)
		m_jobst = JOB_RESPERROR;
	PELOG_LOG((PLV_VERBOSE, "Client complete\n"));
	if (m_server.size() == 0)	// client and server both completed
		m_ahosts->jobComplete(this);
	// if m_server.size() == 0, do not have to cancel servers from here 
	return 0;
}

int AHostsJob::serverComplete(DnsServer *server, aulddays::abuf<char> &response)
{
	// remove server
	if (server != NULL)
	{
		auto iserver = m_server.find(server);
		if (iserver == m_server.end())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server ptr\n"), 1);
		m_server.erase(iserver);
		m_finished.push_back(server);
	}
	PELOG_LOG((PLV_VERBOSE, "Server complete\n"));
	// deal with the response
	if (m_status < JOB_GOTANSWER)
	{
		if (response.size() > 0)
		{
			//PELOG_LOG((PLV_DEBUG, "Dump response(" PL_SIZET "):\n", response.size()));
			//dumpMessage(response);
			m_tanswer = boost::posix_time::microsec_clock::local_time();
			abuf<char> dcompresp;
			if (codecMessage(false, response, dcompresp))
			{
				// decompress failed, bypass the cache and handler, but still send to client
				PELOG_LOG((PLV_WARNING, "Decompress response failed. bypass to client.\n"));
				m_jobst = JOB_RESPERROR;
			}
			else
			{
				PELOG_LOG((PLV_DEBUG, "Decompressed response(" PL_SIZET "):\n", dcompresp.size()));
				dumpMessage(dcompresp, false);

				// TODO: call handler

				codecMessage(true, dcompresp, response);
				//PELOG_LOG((PLV_DEBUG, "Recompressed response(" PL_SIZET "):\n", response.size()));
				//dumpMessage(response);
				if (server)	// write to cache, only when got from a real server (not cache)
				{
					int ttl = manageTtl(response, 0, 0);
					m_ahosts->m_cache.set(m_reqNameType, m_reqNameType.size(), response.buf(), response.size(), ttl);
				}
			}
			int oldstatus = m_status;
			m_status = JOB_GOTANSWER;
			if (oldstatus != JOB_EARLYRET)
				m_client->response(response);
			if (m_server.size() > 0)	// cancel other server since we've got answer
			{
				PELOG_LOG((PLV_VERBOSE, "Canceling all pending servers\n"));
				for (auto i = m_server.begin(); i != m_server.end(); ++i)
					(*i)->cancel();
			}
		}
		else if (m_server.size() == 0)	// all server finished and no answer
		{
			//m_tanswer = boost::posix_time::microsec_clock::local_time();
			int oldstatus = m_status;
			m_status = JOB_GOTANSWER;
			if (oldstatus != JOB_EARLYRET)
				m_client->cancel();
		}
	}
	if (m_status == JOB_GOTANSWER && m_client->responded())	// client early returned
		m_status = JOB_RESPONDED;
	if (m_server.size() == 0 && m_status == JOB_RESPONDED)
	{
		m_ahosts->jobComplete(this);
	}
	return 0;
}

int AHostsJob::heartBeat(const boost::posix_time::ptime &now)
{
	int ret = 0;
	static const int EARLYRET_THRESHOLD = 1500;
	if (m_status == JOB_REQUESTING)
	{
		int64_t passed = (now - m_tstart).total_milliseconds();
		if (passed > EARLYRET_THRESHOLD || passed < (0 - EARLYRET_THRESHOLD))
		{
			// Some time has passed, if there is (expired) result in cache, return it
			time_t uptime;
			int32_t ttl = 0;
			if (m_questionNum == 1 &&
				m_ahosts->m_cache.get(m_reqNameType, m_reqNameType.size(), m_cached, uptime, ttl) == 0)
			{
				PELOG_LOG((PLV_INFO, "Early return %s %d:%d.\n",
					m_reqPrint.buf(), int(passed), EARLYRET_THRESHOLD));
				m_status = JOB_EARLYRET;
				m_tearly = boost::posix_time::microsec_clock::local_time();
				//m_early = true;
				m_client->response(m_cached);
			}
			else
				m_status = JOB_NOEARLYRET;
		}
	}
	for (auto i = m_server.begin(); i != m_server.end(); ++i)
	{
		int res = (*i)->heartBeat(now);
		if (res > 0)
			m_jobst = JOB_TIMEOUT;
		else if (res < 0)
			ret = res;
	}
	int res = m_client->heartBeat(now);
	if (res > 0)
		m_jobst = JOB_RESPERROR;
	else if (res < 0)
		ret = res;
	return ret;
}

void AHostsJob::finished()
{
	// print status log for this job
	static const char *jobstNames[] = {
		"OK", "REQ_ERROR", "TIMEOUT", "RESPERROR"
	};
	BOOST_STATIC_ASSERT(sizeof(jobstNames) / sizeof(jobstNames[0]) == JOB_STNUM);
	m_tfin = boost::posix_time::microsec_clock::local_time();
	PELOG_LOG((PLV_INFO, "STATUS req(%s) res(%s), tely(%d) tans(%d) trpl(%d) tfin(%d)\n",
		m_reqPrint.buf(), jobstNames[m_jobst],
		(int)(m_tearly - m_tstart).total_milliseconds(),
		(int)(m_tanswer - m_tstart).total_milliseconds(),
		(int)(m_treply - m_tstart).total_milliseconds(),
		(int)(m_tfin - m_tstart).total_milliseconds()));
}

// UdpServer
int UdpServer::send(aulddays::abuf<char> &req)
{
	m_start = boost::posix_time::microsec_clock::local_time();
	// look for an available local port
	boost::uniform_int<> randport(16385, 32767);
	bool bindok = false;
	for (int i = 0; i < 10; ++i)
	{
		asio::error_code ec;
		if (!m_socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), randport(AHostsRand)), ec))
		{
			bindok = true;
			break;
		}
		else
		{
			PELOG_LOG((PLV_VERBOSE, "Bind failed %d:%s\n", ec.value(), ec.message().c_str()));
		}
	}
	if (!bindok)
	{
		PELOG_LOG((PLV_ERROR, "Failed to bind when sending recursive request\n"));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
		return 1;
	}
	m_req.scopyFrom(req);
	// assign a new id to request
	boost::uniform_int<> randid(1024, uint16_t(-1) - 1);
	m_id = randid(AHostsRand);
	*(uint16_t *)(char *)m_req = htons(m_id);
	PELOG_LOG((PLV_DEBUG, "To send to server %s:%d on %d. id(%d) size(" PL_SIZET ")\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), (int)m_socket.local_endpoint().port(), m_id, req.size()));
	m_status = SERVER_SENDING;
	m_socket.async_send_to(asio::buffer(m_req, m_req.size()), m_remote,
		boost::bind(&UdpServer::onReqSent, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpServer::onReqSent(const asio::error_code& error, size_t size)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Send request to server failed. %s\n", error.message().c_str()));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
		return;
	}
	m_res.resize(0);
	PELOG_LOG((PLV_DEBUG, "Request sent (" PL_SIZET "), waiting server response\n", size));
	m_status = SERVER_WAITING;
	// since we do not know the response size yet, we just peek with empty buffer at first to get the size
	m_socket.async_receive_from(asio::buffer(m_res, 0), m_remote, MSG_PEEK,
		boost::bind(&UdpServer::onResponse, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
}

void UdpServer::onResponse(const asio::error_code& error, size_t size)
{
	if (error && !m_cancel && m_res.size() == 0 && error.value() == ERROR_MORE_DATA)	// peek result
	{
		m_res.resize(m_socket.available());
		PELOG_LOG((PLV_DEBUG, "Response size from server " PL_SIZET "\n", m_res.size()));
		m_socket.async_receive_from(asio::buffer(m_res, m_res.size()), m_remote,
			boost::bind(&UdpServer::onResponse, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
		return;
	}
	if (error)
	{
		if (!m_cancel)
		{
			if (m_res.size() == 0)	// The peek was performed
			PELOG_LOG((PLV_ERROR, "Recv response from server failed. %d:%s\n", error.value(), error.message().c_str()));
		}
		m_res.resize(0);
	}
	else if (m_id != ntohs(*(const uint16_t *)(const char *)m_res))
	{
		PELOG_LOG((PLV_ERROR, "Invalid ID from server. expect %d got %d size(" PL_SIZET ")\n",
			(int)m_id, (int)ntohs(*(const uint16_t *)(const char *)m_res), size));
		m_res.resize(0);
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Response from server got id(%d), size(" PL_SIZET ")\n", (int)m_id, size));
		m_res.resize(size);
	}
	m_status = SERVER_GOTANSWER;
	m_job->serverComplete(this, m_res);
}

int UdpServer::cancel()
{
	PELOG_LOG((PLV_VERBOSE, "Server cenceling.\n"));
	m_cancel = true;
	if (m_status < SERVER_SENDING)
	{
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
	}
	else if (m_status == SERVER_SENDING || m_status == SERVER_WAITING)
		m_socket.close();
	return 0;
}

// return: 0 ok, >0 timedout, <0 error
int UdpServer::heartBeat(const boost::posix_time::ptime &now)
{
	int ret = 0;
	if (m_status >= SERVER_SENDING && m_status <= SERVER_WAITING)
	{
		int64_t passed = (now - m_start).total_milliseconds();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Server timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
			ret = 1;
		}
	}
	return ret;
}

// UdpClient
UdpClient::UdpClient(AHostsJob *job, asio::io_service &ioService)
	: DnsClient(job, ioService, 5000), m_socket(ioService, asio::ip::udp::v4()),
	m_start(boost::posix_time::min_date_time), m_cancel(false)
{
}

int UdpClient::run(const abuf<char> &req, const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote)
{
	m_remote = remote;
	asio::error_code ec;
	asio::socket_base::reuse_address option(true);
	m_socket.set_option(option);
	m_socket.bind(asio::ip::udp::endpoint(socket.local_endpoint()), ec);
	if (ec)
		PELOG_ERROR_RETURN((PLV_ERROR, "Socket bind failed. %s\n", ec.message().c_str()), -1);
	m_id = ntohs(*(const uint16_t *)(const char *)req);
	m_status = CLIENT_WAITING;
	return m_job->request(req);
}

int UdpClient::response(aulddays::abuf<char> &res)
{
	if (m_status != CLIENT_WAITING)
		PELOG_ERROR_RETURN((PLV_ERROR, "Client in invalid status %d\n", m_status), 1);
	// write back original id
	*(uint16_t *)(char *)res = htons(m_id);
	m_start = boost::posix_time::microsec_clock::local_time();
	m_status = CLIENT_RESPONDING;
	PELOG_LOG((PLV_DEBUG, "To send to client %s:%d on %d. size " PL_SIZET "\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), (int)m_socket.local_endpoint().port(), res.size()));
	m_socket.async_send_to(asio::buffer(res, res.size()), m_remote,
		boost::bind(&UdpClient::onResponsed, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpClient::onResponsed(const asio::error_code& error, size_t size)
{
	if (error)
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Send response to client failed. %s\n", error.message().c_str()));
	else
		PELOG_LOG((PLV_DEBUG, "Response sent to client (" PL_SIZET ")\n", size));
	asio::error_code ec;
	m_socket.close(ec);
	if (m_status != CLIENT_RESPONDING)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Client in invalid status %d\n", m_status));
	m_status = CLIENT_RESPONDED;
	m_job->clientComplete(this, error ? -1 : 0);
}

int UdpClient::cancel()
{
	PELOG_LOG((PLV_VERBOSE, "Cancel client\n"));
	asio::error_code ec;
	m_cancel = true;
	if (m_status <= CLIENT_WAITING)
	{
		// do not respond. client would natually timeout
		m_status = CLIENT_RESPONDED;
		m_socket.close(ec);
		m_job->clientComplete(this, 1);
	}
	else if (m_status == CLIENT_RESPONDING)
	{
		m_socket.close(ec);	// cancel() may not work on some system, so just close
	}
	else
	{
		assert(m_status == CLIENT_RESPONDED);
		PELOG_LOG((PLV_ERROR, "Trying to cancel a completed client\n"));
		return 1;
	}
	return 0;
}

// return: 0 ok, >0 timedout, <0 error
int UdpClient::heartBeat(const boost::posix_time::ptime &now)
{
	int ret = 0;
	if (m_status == CLIENT_RESPONDING)
	{
		int64_t passed = (now - m_start).total_milliseconds();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Client timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
			ret = 1;
		}
	}
	return ret;
}

