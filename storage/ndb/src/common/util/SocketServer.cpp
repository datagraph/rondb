/*
   Copyright (c) 2003, 2023, Oracle and/or its affiliates.
   Copyright (c) 2022, 2023, Hopsworks and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include <ndb_global.h>

#include <SocketServer.hpp>

#include <NdbTCP.h>
#include <NdbOut.hpp>
#include <NdbThread.h>
#include <NdbSleep.h>
#include <NdbTick.h>
#include "ndb_socket.h"
#include <OwnProcessInfo.hpp>
#include <EventLogger.hpp>
#include "portlib/ndb_sockaddr.h"

#if 0
#define DEBUG_FPRINTF(arglist) do { fprintf arglist ; } while (0)
#else
#define DEBUG_FPRINTF(a)
#endif

SocketServer::SocketServer(unsigned maxSessions) :
  m_sessions(10),
  m_services(5),
  m_maxSessions(maxSessions),
  m_use_only_ipv4(false),
  m_stopThread(false),
  m_thread(nullptr)
{
}

SocketServer::~SocketServer() {
  unsigned i;
  for(i = 0; i<m_sessions.size(); i++){
    Session* session= m_sessions[i].m_session;
    assert(session->m_refCount == 0);
    delete session;
  }
  for(i = 0; i<m_services.size(); i++){
    if(ndb_socket_valid(m_services[i].m_socket))
      ndb_socket_close(m_services[i].m_socket);
    delete m_services[i].m_service;
  }
}

<<<<<<< HEAD
bool SocketServer::tryBind(unsigned short port,
                           bool use_only_ipv4,
                           const char* intface,
                           char* error,
                           size_t error_size)
{
  DEBUG_FPRINTF((stderr, "SocketServer::tryBind, intface: %s\n", intface));
  while (!use_only_ipv4)
=======
bool SocketServer::tryBind(ndb_sockaddr servaddr,
                           char* error, size_t error_size) {
  const ndb_socket_t sock = ndb_socket_create(servaddr.get_address_family());

  if (!ndb_socket_valid(sock))
    return false;

  if (servaddr.need_dual_stack())
  {
    [[maybe_unused]] bool ok = ndb_socket_dual_stack(sock, 1);
  }

  DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));

  if (ndb_socket_configure_reuseaddr(sock, true) == -1)
>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6
  {
    struct sockaddr_in6 servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_any;
    servaddr.sin6_port = htons(port);

<<<<<<< HEAD
    if (intface != nullptr)
    {
      if(Ndb_getInAddr6(&servaddr.sin6_addr, intface))
      {
        DEBUG_FPRINTF((stderr, "Failed Ndb_getInAddr6\n"));
        break;
      }
    }

    ndb_socket_t sock;
    ndb_socket_create_dual_stack(sock, SOCK_STREAM, 0);
    if (!ndb_socket_valid(sock))
    {
      DEBUG_FPRINTF((stderr, "Failed to create socket\n"));
      break;
    }

    DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));
    DEBUG_FPRINTF((stderr, "NDB_SOCKET: %s\n",
                   ndb_socket_to_string(sock).c_str()));

    if (ndb_socket_configure_reuseaddr(sock, true) == -1)
    {
      DEBUG_FPRINTF((stderr, "Failed call to reuse address IPv6, errno: %d\n",
                     errno));
      ndb_socket_close(sock);
      break;
    }

    if (ndb_bind_inet(sock, &servaddr) == -1) {
      if (error != nullptr) {
        int err_code = ndb_socket_errno();
        snprintf(error, error_size, "%d '%s'", err_code,
                 ndb_socket_err_message(err_code).c_str());
      }
      DEBUG_FPRINTF((stderr, "Failed call to bind address\n"));
      ndb_socket_close(sock);
      break;
=======
  if (ndb_bind(sock, &servaddr) == -1) {
    if (error != nullptr) {
      int err_code = ndb_socket_errno();
      snprintf(error, error_size, "%d '%s'", err_code,
               ndb_socket_err_message(err_code).c_str());
>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6
    }
    ndb_socket_close(sock);
    return true;
  }
  /**
   * If configured to use IPv4 only or after failure of IPv6 socket
   * we will also try with an IPv4 socket.
   */
  {
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (intface != nullptr)
    {
      if(Ndb_getInAddr(&servaddr.sin_addr, intface))
      {
        DEBUG_FPRINTF((stderr, "Failed Ndb_getInAddr\n"));
        return false;
      }
    }

    ndb_socket_t sock;
    ndb_socket_create_ipv4(sock, SOCK_STREAM, 0);
    if (!ndb_socket_valid(sock))
    {
      DEBUG_FPRINTF((stderr, "Failed to create socket\n"));
      return false;
    }

    DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));

    if (ndb_socket_configure_reuseaddr(sock, true) == -1)
    {
      DEBUG_FPRINTF((stderr, "Failed call to reuse address IPv4, errno: %d\n",
                     errno));
      ndb_socket_close(sock);
      return false;
    }

    if (ndb_bind_inet4(sock, &servaddr) == -1) {
      if (error != nullptr) {
        int err_code = ndb_socket_errno();
        snprintf(error, error_size, "%d '%s'", err_code,
                 ndb_socket_err_message(err_code).c_str());
      }
      DEBUG_FPRINTF((stderr, "Failed call to bind address\n"));
      ndb_socket_close(sock);
      return false;
    }
    ndb_socket_close(sock);
  }
  return true;
}

#define MAX_SOCKET_SERVER_TCP_BACKLOG 64
bool
SocketServer::setup(SocketServer::Service * service, ndb_sockaddr* servaddr)
{
  DBUG_ENTER("SocketServer::setup");
<<<<<<< HEAD
  DEBUG_FPRINTF((stderr, "SocketServer::setup\n"));
  DBUG_PRINT("enter",("interface=%s, port=%u", intface, *port));
  DEBUG_FPRINTF((stderr, "interface=%s, port=%u\n", intface, *port));
  ndb_socket_t sock;
  bool use_ipv4_setup = true;
  while (!m_use_only_ipv4)
  {
    struct sockaddr_in6 servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_any;
    servaddr.sin6_port = htons(*port);

    if(intface != nullptr)
    {
      if (Ndb_getInAddr6(&servaddr.sin6_addr, intface))
      {
        DEBUG_FPRINTF((stderr, "Failed Ndb_getInAddr6\n"));
        break;
      }
    }

    ndb_socket_create_dual_stack(sock, SOCK_STREAM, 0);
    if (!ndb_socket_valid(sock))
    {
      DBUG_PRINT("error",("socket() - %d - %s",
        socket_errno, strerror(socket_errno)));
      DEBUG_FPRINTF((stderr, "socket() - %d - %s\n",
                     socket_errno, strerror(socket_errno)));
      break;
    }

    DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));
    DEBUG_FPRINTF((stderr,"NDB_SOCKET: %s\n",
                   ndb_socket_to_string(sock).c_str()));

    if (ndb_socket_reuseaddr(sock, true) == -1)
    {
      DBUG_PRINT("error",("setsockopt() - %d - %s",
        errno, strerror(errno)));
      DEBUG_FPRINTF((stderr, "setsockopt() - %d - %s\n",
                     errno, strerror(errno)));
      ndb_socket_close(sock);
      break;
    }

    if (ndb_bind_inet(sock, &servaddr) == -1) {
      DBUG_PRINT("error",("bind() - %d - %s",
        socket_errno, strerror(socket_errno)));
      DEBUG_FPRINTF((stderr, "bind() - %d - %s\n",
        socket_errno, strerror(socket_errno)));
      ndb_socket_close(sock);
      break;
    }

    /* Get the address and port we bound to */
    struct sockaddr_in6 serv_addr;
    if(ndb_getsockname(sock, &serv_addr))
    {
      g_eventLogger->info(
          "An error occurred while trying to find out what port we bound to."
          " Error: %d - %s",
          ndb_socket_errno(), strerror(ndb_socket_errno()));
      ndb_socket_close(sock);
      break;
    }
    *port = ntohs(serv_addr.sin6_port);
    setOwnProcessInfoServerAddress((sockaddr*)& serv_addr);
    DEBUG_FPRINTF((stderr, "Successful setup of IPv6 setup\n"));
    use_ipv4_setup = false;
    break;
  }
  /**
   * If configured to use IPv4 only or after failure of IPv6 socket
   * we will also try with an IPv4 socket.
   */
  if (use_ipv4_setup)
  {
    DEBUG_FPRINTF((stderr, "Setup of IPv4 setup starting\n"));
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(*port);

    if(intface != 0)
    {
      if (Ndb_getInAddr(&servaddr.sin_addr, intface))
      {
        DEBUG_FPRINTF((stderr, "Failed Ndb_getInAddr\n"));
        DBUG_RETURN(false);
      }
    }

    ndb_socket_create_ipv4(sock, SOCK_STREAM, 0);
    if (!ndb_socket_valid(sock))
    {
      DBUG_PRINT("error",("socket() - %d - %s",
        socket_errno, strerror(socket_errno)));
      DEBUG_FPRINTF((stderr, "socket() - %d - %s\n",
                     socket_errno, strerror(socket_errno)));
      DBUG_RETURN(false);
    }

    DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));
    DEBUG_FPRINTF((stderr,"NDB_SOCKET: %s\n",
                   ndb_socket_to_string(sock).c_str()));

    if (ndb_socket_reuseaddr(sock, true) == -1)
    {
      DBUG_PRINT("error",("setsockopt() - %d - %s",
        errno, strerror(errno)));
      DEBUG_FPRINTF((stderr, "setsockopt() - %d - %s\n",
                     errno, strerror(errno)));
      ndb_socket_close(sock);
      DBUG_RETURN(false);
    }
=======

  const ndb_socket_t sock = ndb_socket_create(servaddr->get_address_family());

  if (!ndb_socket_valid(sock))
  {
    DBUG_PRINT("error",("socket() - %d - %s",
      socket_errno, strerror(socket_errno)));
    DBUG_RETURN(false);
  }

  if (servaddr->need_dual_stack())
  {
    [[maybe_unused]] bool ok = ndb_socket_dual_stack(sock, 1);
  }

  DBUG_PRINT("info",("NDB_SOCKET: %s", ndb_socket_to_string(sock).c_str()));
>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6

    if (ndb_bind_inet4(sock, &servaddr) == -1) {
      DBUG_PRINT("error",("bind() - %d - %s",
        socket_errno, strerror(socket_errno)));
      DEBUG_FPRINTF((stderr, "bind() - %d - %s\n",
                     socket_errno, strerror(socket_errno)));
      ndb_socket_close(sock);
      DBUG_RETURN(false);
    }

<<<<<<< HEAD
    /* Get the address and port we bound to */
    struct sockaddr_in serv_addr;
    if(ndb_getsockname4(sock, &serv_addr))
    {
      g_eventLogger->info(
=======
  if (ndb_bind(sock, servaddr) == -1) {
    DBUG_PRINT("error",("bind() - %d - %s",
      socket_errno, strerror(socket_errno)));
    ndb_socket_close(sock);
    DBUG_RETURN(false);
  }

  /* Get the address and port we bound to */
  if(ndb_getsockname(sock, servaddr))
  {
    g_eventLogger->info(
>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6
        "An error occurred while trying to find out what port we bound to."
        " Error: %d - %s",
        ndb_socket_errno(), strerror(ndb_socket_errno()));
      ndb_socket_close(sock);
      DBUG_RETURN(false);
    }
    *port = ntohs(serv_addr.sin_port);
    setOwnProcessInfoServerAddress4((sockaddr*)& serv_addr);
    DEBUG_FPRINTF((stderr, "Successful setup of IPv4 setup\n"));
  }
<<<<<<< HEAD
  DBUG_PRINT("info",("bound to %u", *port));
  DEBUG_FPRINTF((stderr, "bound to %u\n", *port));
=======
  setOwnProcessInfoServerAddress(servaddr);

  DBUG_PRINT("info",("bound to %u", servaddr->get_port()));
>>>>>>> 057f5c9509c6c9ea3ce3acdc619f3353c09e6ec6

  if (ndb_listen(sock, m_maxSessions > MAX_SOCKET_SERVER_TCP_BACKLOG ?
                      MAX_SOCKET_SERVER_TCP_BACKLOG : m_maxSessions) == -1)
  {
    DBUG_PRINT("error",("listen() - %d - %s",
      socket_errno, strerror(socket_errno)));
    DEBUG_FPRINTF((stderr, "listen() - %d - %s\n",
                   socket_errno, strerror(socket_errno)));
    ndb_socket_close(sock);
    DBUG_RETURN(false);
  }

  DEBUG_FPRINTF((stderr, "Listening on port: %u\n",
                (Uint32)*port));

  ServiceInstance i;
  i.m_socket = sock;
  i.m_service = service;
  m_services.push_back(i);

  // Increase size to allow polling all listening ports
  m_services_poller.set_max_count(m_services.size());

  DBUG_RETURN(true);
}


bool
SocketServer::doAccept()
{
  m_services.lock();

  m_services_poller.clear();
  for (unsigned i = 0; i < m_services.size(); i++)
  {
    m_services_poller.add_readable(m_services[i].m_socket); // Need error ??
  }
  assert(m_services.size() == m_services_poller.count());

  const int accept_timeout_ms = 1000;
  const int ret = m_services_poller.poll(accept_timeout_ms);
  if (ret < 0)
  {
    // Error occurred, indicate error to caller by returning false
    m_services.unlock();
    return false;
  }

  if (ret == 0)
  {
    // Timeout occurred
    m_services.unlock();
    return true;
  }

  bool result = true;
  for (unsigned i = 0; i < m_services_poller.count(); i++)
  {
    const bool has_read = m_services_poller.has_read(i);

    if (!has_read)
      continue; // Ignore events where read flag wasn't set

    ServiceInstance & si = m_services[i];
    assert(m_services_poller.is_socket_equal(i, si.m_socket));

    const ndb_socket_t childSock = ndb_accept(si.m_socket, nullptr);
    if (!ndb_socket_valid(childSock))
    {
      DEBUG_FPRINTF((stderr,"NDB_SOCKET failed accept: %s\n",
                     ndb_socket_to_string(si.m_socket).c_str()));
      // Could not 'accept' socket(maybe at max fds), indicate error
      // to caller by returning false
      result = false;
      continue;
    }

    SessionInstance s;
    s.m_service = si.m_service;
    s.m_session = si.m_service->newSession(childSock);
    if (s.m_session != nullptr)
    {
      m_session_mutex.lock();
      m_sessions.push_back(s);
      startSession(m_sessions.back());
      m_session_mutex.unlock();
    }
  }

  m_services.unlock();
  return result;
}

extern "C"
void* 
socketServerThread_C(void* _ss){
  SocketServer * ss = (SocketServer *)_ss;
  ss->doRun();
  return nullptr;
}

struct NdbThread*
SocketServer::startServer()
{
  m_threadLock.lock();
  if(m_thread == nullptr && m_stopThread == false)
  {
    m_thread = NdbThread_Create(socketServerThread_C,
				(void**)this,
                                0, // default stack size
				"NdbSockServ",
				NDB_THREAD_PRIO_LOW);
  }
  m_threadLock.unlock();
  return m_thread;
}

void
SocketServer::stopServer(){
  m_threadLock.lock();
  if(m_thread != nullptr){
    m_stopThread = true;
    
    void * res;
    NdbThread_WaitFor(m_thread, &res);
    NdbThread_Destroy(&m_thread);
    m_thread = nullptr;
  }
  m_threadLock.unlock();
}

void
SocketServer::doRun(){

  while(!m_stopThread){
    m_session_mutex.lock();
    checkSessionsImpl();
    m_session_mutex.unlock();

    if(m_sessions.size() >= m_maxSessions){
      // Don't accept more connections yet
      DEBUG_FPRINTF((stderr, "Too many connections\n"));
      NdbSleep_MilliSleep(200);
      continue;
    }

    if (!doAccept()){
      // accept failed, step back
      DEBUG_FPRINTF((stderr, "Accept failed\n"));
      NdbSleep_MilliSleep(200);
    }
  }
}

void
SocketServer::startSession(SessionInstance & si){
  si.m_thread = NdbThread_Create(sessionThread_C,
				 (void**)si.m_session,
                                 0, // default stack size
				 "NdbSock_Session",
				 NDB_THREAD_PRIO_LOW);
}

void
SocketServer::foreachSession(void (*func)(SocketServer::Session*, void *),
                             void *data)
{
  // Build a list of pointers to all active sessions
  // and increase refcount on the sessions
  m_session_mutex.lock();
  Vector<Session*> session_pointers(m_sessions.size());
  for(unsigned i= 0; i < m_sessions.size(); i++){
    Session* session= m_sessions[i].m_session;
    session_pointers.push_back(session);
    session->m_refCount++;
  }
  m_session_mutex.unlock();

  // Call the function on each session
  for(unsigned i= 0; i < session_pointers.size(); i++){
    (*func)(session_pointers[i], data);
  }

  // Release the sessions pointers and any stopped sessions
  m_session_mutex.lock();
  for(unsigned i= 0; i < session_pointers.size(); i++){
    Session* session= session_pointers[i];
    assert(session->m_refCount > 0);
    session->m_refCount--;
  }
  checkSessionsImpl();
  m_session_mutex.unlock();
}

void
SocketServer::checkSessions()
{
  m_session_mutex.lock();
  checkSessionsImpl();
  m_session_mutex.unlock();  
}

void
SocketServer::checkSessionsImpl()
{
  for(int i = m_sessions.size() - 1; i >= 0; i--)
  {
    if(m_sessions[i].m_session->m_thread_stopped &&
       (m_sessions[i].m_session->m_refCount == 0))
    {
      if(m_sessions[i].m_thread != nullptr)
      {
	void* ret;
	NdbThread_WaitFor(m_sessions[i].m_thread, &ret);
	NdbThread_Destroy(&m_sessions[i].m_thread);
      }
      m_sessions[i].m_session->stopSession();
      delete m_sessions[i].m_session;
      m_sessions.erase(i);
    }
  }
}

bool
SocketServer::stopSessions(bool wait, unsigned wait_timeout){
  int i;
  m_session_mutex.lock();
  for(i = m_sessions.size() - 1; i>=0; i--)
  {
    m_sessions[i].m_session->stopSession();
  }
  m_session_mutex.unlock();
  
  for(i = m_services.size() - 1; i>=0; i--)
    m_services[i].m_service->stopSessions();
  
  if(!wait)
    return false; // No wait

  const NDB_TICKS start = NdbTick_getCurrentTicks();
  m_session_mutex.lock();
  while(m_sessions.size() > 0){
    checkSessionsImpl();
    m_session_mutex.unlock();

    if (wait_timeout > 0 &&
        NdbTick_Elapsed(start,NdbTick_getCurrentTicks()).milliSec() > wait_timeout)
      return false; // Wait abandoned

    NdbSleep_MilliSleep(100);
    m_session_mutex.lock();
  }
  m_session_mutex.unlock();
  return true; // All sessions gone
}


/***** Session code ******/

extern "C"
void* 
sessionThread_C(void* _sc){
  SocketServer::Session * si = (SocketServer::Session *)_sc;

  assert(si->m_thread_stopped == false);

  if(!si->m_stop)
    si->runSession();
  else
  {
    ndb_socket_close(si->m_socket);
    ndb_socket_invalidate(&si->m_socket);
  }

  // Mark the thread as stopped to allow the
  // session resources to be released
  si->m_thread_stopped = true;
  return nullptr;
}

template class MutexVector<SocketServer::ServiceInstance>;
template class Vector<SocketServer::SessionInstance>;
template class Vector<SocketServer::Session*>;
