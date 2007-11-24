/* $Header$ */
/* 
 * MBDyn (C) is a multibody analysis code. 
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2007
 *
 * Pierangelo Masarati	<masarati@aero.polimi.it>
 * Paolo Mantegazza	<mantegazza@aero.polimi.it>
 *
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 * via La Masa, 34 - 20156 Milano, Italy
 * http://www.aero.polimi.it
 *
 * Changing this copyright notice is forbidden.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 * 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <mbconfig.h>           /* This goes first in every *.c,*.cc file */
#endif /* HAVE_CONFIG_H */

#ifdef USE_SOCKET

#include "myassert.h"
#include "mynewmem.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#include "usesock.h"
#include "sock.h"

#define DEFAULT_PORT	5500 /* FIXME: da definire meglio */
#define DEFAULT_HOST 	"127.0.0.1"

UseSocket::UseSocket(bool c)
: sock(-1),
create(c),
connected(false),
abandoned(false),
socklen(0)
{
	NO_OP;
}

UseSocket::~UseSocket(void)
{
	if (sock != -1) {
		int status;

		status = shutdown(sock, SHUT_RDWR);
		
		time_t t = time(NULL);
		pedantic_cout("UseSocket::~UseSocket: shutdown: "
			"socket=" << sock
			<< " status=" << status
			<< " time=" << asctime(localtime(&t))
			<< std::endl);

		if (status < 0) {
			int save_errno = errno;
			char *msg = strerror(save_errno);

			silent_cerr("UseSocket::~UseSocket: shutdown error "
				"(" << save_errno << ": " << msg << ")"
				<< std::endl);
		}

		status = close(sock);
		t = time(NULL);

		pedantic_cout("UseSocket::~UseSocket: close: "
			"socket=" << sock
			<< " status=" << status
			<< " time=" << asctime(localtime(&t))
			<< std::endl);
	}
}

std::ostream&
UseSocket::Restart(std::ostream& out) const
{
	if (create) {
		out << ", create, yes";
	}
	return out;
}

int
UseSocket::GetSock(void) const
{
	return sock;
}

void
UseSocket::SetSock(int s)
{
	sock = s;
}

void
UseSocket::PostConnect(void)
{
	struct linger lin;
	lin.l_onoff = 1;
	lin.l_linger = 0;
	
	if (setsockopt(GetSock(), SOL_SOCKET, SO_LINGER, &lin, sizeof(lin))) {
		int save_errno = errno;
		char *msg = strerror(save_errno);

      		silent_cerr("UseSocket::PostConnect: setsockopt() failed "
			"(" << save_errno << ": " << msg << ")"
			<< std::endl);
      		throw ErrGeneric();
	}
}

void
UseSocket::Connect(void)
{
	// FIXME: retry strategy should be configurable
	int	count = 1000;
	int	timeout = 100000;
	
	for ( ; count > 0; count--) {
		if (connect(sock, GetSockaddr(), GetSocklen()) < 0) {
			int save_errno = errno;
			if (save_errno == ECONNREFUSED) {
				/* Socket does not exist yet; retry */
				usleep(timeout);
				continue;
			}

			/* Connect failed */
			char *msg = strerror(save_errno);
			silent_cerr("UseSocket::Connect: connect() failed "
				"(" << save_errno << ": " << msg << ")"
				<< std::endl);
			throw ErrGeneric();
		}

		/* Success */
		connected = true;
		PostConnect();
		return;
	}

	silent_cerr("UseSocket(): connection timed out"
		<< std::endl);
	throw ErrGeneric();
}

void
UseSocket::ConnectSock(int s)
{
	SetSock(s);

	connected = true;

	PostConnect();
}

bool
UseSocket::Connected(void) const
{
	return connected;
}

void
UseSocket::Abandon(void)
{
	abandoned = true;
}

bool
UseSocket::Abandoned(void) const
{
	return abandoned;
}

socklen_t&
UseSocket::GetSocklen(void) const
{
	return socklen;
}

UseInetSocket::UseInetSocket(const char *const h, unsigned short p, bool c)
: UseSocket(c),
host(0),
port(p)
{
   	ASSERT(port > 0);

	socklen = sizeof(addr);

	char buf[256];

	/* if 0, means INET on localhost */
	if (h) {
		SAFESTRDUP(host, h);
		snprintf(buf, sizeof(buf), "%s:%u", host, port);

	} else {
		snprintf(buf, sizeof(buf), "%s:%u", DEFAULT_HOST, port);
	}
	
	if (create) {
		struct sockaddr_in	addr_name;
		int			save_errno;
		
		/*Set everything to zero*/
		memset(&addr_name, 0, sizeof(addr_name));

   		sock = mbdyn_make_inet_socket(&addr_name, host, port, 1, 
				&save_errno);
		
		if (sock == -1) {
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseInetSocket(" << buf << "): "
				"socket() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();

   		} else if (sock == -2) {
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseInetSocket(" << buf << "): "
				"bind() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();

   		} else if (sock == -3) {
      			silent_cerr("UseInetSocket(" << buf << "): "
				"illegal host name \"" << host << "\" "
				"(" << save_errno << ")"
				<< std::endl);
      			throw ErrGeneric();
   		}

   		if (listen(sock, 1) < 0) {
			save_errno = errno;
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseInetSocket(" << buf << "): "
				"listen() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();
   		}
	}	 
}

UseInetSocket::~UseInetSocket(void)
{
	if (host) {
		SAFEDELETEARR(host);
		host = 0;
	}
}

std::ostream&
UseInetSocket::Restart(std::ostream& out) const
{
	UseSocket::Restart(out);
	out << ", port, " << port;
	if (host) {
		out << ", host, " << "\"" << host << "\"";
	}
	return out;
}

void
UseInetSocket::Connect(void)
{
	if (connected) {
		return;
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		const char *h = host;
		if (h == 0) {
			h = DEFAULT_HOST;
		}
		silent_cerr("UseSocket(): socket() failed "
				<< "\"" << h << ":" << port << "\""
				<< std::endl);
		throw ErrGeneric();
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_aton(host, &addr.sin_addr) == 0) {
		const char *h = host;
		if (h == 0) {
			h = DEFAULT_HOST;
		}
		silent_cerr("UseSocket(): unknown host "
				"\"" << h << ":" << port << "\""
				<< std::endl);
		throw ErrGeneric();	
	}

	pedantic_cout("connecting to inet socket "
			"\"" << inet_ntoa(addr.sin_addr) 
			<< ":" << ntohs(addr.sin_port)
			<< "\" ..." << std::endl);

	UseSocket::Connect();
}
	
void
UseInetSocket::ConnectSock(int s)
{
	UseSocket::ConnectSock(s);
	
	silent_cout("INET connection to port=" << port << " by client "
			<< "\"" << inet_ntoa(addr.sin_addr)
			<< ":" << ntohs(addr.sin_port) << "\""
			<< std::endl);
}

struct sockaddr *
UseInetSocket::GetSockaddr(void) const
{
	return (struct sockaddr *)&addr;
}
	
UseLocalSocket::UseLocalSocket(const char *const p, bool c)
: UseSocket(c),
path(0)
{
	ASSERT(p);

	socklen = sizeof(addr);
	
	SAFESTRDUP(path, p);

	if (create) {
		int		save_errno;

		sock = mbdyn_make_named_socket(path, 1, &save_errno);
		
		if (sock == -1) {
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseLocalSocket(" << path << "): "
				"socket() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();

   		} else if (sock == -2) {
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseLocalSocket(" << path << "): "
				"bind() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();
   		}
   		if (listen(sock, 1) < 0) {
			save_errno = errno;
			const char	*err_msg = strerror(save_errno);

      			silent_cerr("UseLocalSocket(" << path << "): "
				"listen() failed "
				"(" << save_errno << ": " << err_msg << ")"
				<< std::endl);
      			throw ErrGeneric();
   		}
	}	 
}

UseLocalSocket::~UseLocalSocket(void)
{
	if (path) {
		if (create) {
			unlink(path);
		}

		SAFEDELETEARR(path);
		path = 0;
	}
}

std::ostream&
UseLocalSocket::Restart(std::ostream& out) const
{
	return UseSocket::Restart(out) << ", path, " << "\"" << path << "\"";
}

void
UseLocalSocket::Connect(void)
{
	if (connected) {
		return;
	}

	sock = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (sock < 0){
		int save_errno = errno;
		char *msg = strerror(save_errno);

		silent_cerr("UseSocket(): socket() failed "
				"(" << save_errno << ": " << msg << ")"
				<< std::endl);
		throw ErrGeneric();
	}
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, path, sizeof(addr.sun_path));

	pedantic_cout("connecting to local socket \""
			<< path << "\" ..." << std::endl);

	UseSocket::Connect();
}

void
UseLocalSocket::ConnectSock(int s)
{
	UseSocket::ConnectSock(s);
	
	silent_cout("LOCAL connection to path=" << path << std::endl);
}

struct sockaddr *
UseLocalSocket::GetSockaddr(void) const
{
	return (struct sockaddr *)&addr;
}

#endif // USE_SOCKET
