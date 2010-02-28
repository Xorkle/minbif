/*
 * Minbif - IRC instant messaging gateway
 * Copyright(C) 2009-2010 Romain Bignon, Marc Dequènes (Duck)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "sockwrap.h"
#include "sockwrap_plain.h"
#ifdef HAVE_TLS
#  include "sockwrap_tls.h"
#endif
#include "core/config.h"
#include "core/util.h"

SockWrapper::SockWrapper(int _recv_fd, int _send_fd) : recv_fd(_recv_fd), send_fd(_send_fd)
{
	if (recv_fd < 0)
		throw SockError::SockError("Wrong input file descriptor");
	if (send_fd < 0)
		throw SockError::SockError("Wrong output file descriptor");
}

SockWrapper::~SockWrapper()
{
	EndSessionCleanup();
}

SockWrapper* SockWrapper::Builder(int _recv_fd, int _send_fd)
{
#ifdef HAVE_TLS
	if (conf.GetSection("irc")->GetSection("daemon")->GetItem("security")->String().compare("ssl") == 0)
		return new SockWrapperTLS(_recv_fd, _send_fd);
#endif
	return new SockWrapperPlain(_recv_fd, _send_fd);
}

string SockWrapper::GetClientHostname()
{
	struct sockaddr_storage sock;
	socklen_t socklen = sizeof(sock);

	/* Get the client's hostname. */
	string clienthost = "localhost.localdomain";
	if(getpeername(recv_fd, (struct sockaddr*) &sock, &socklen) == 0)
	{
		char buf[NI_MAXHOST+1];

		if(getnameinfo((struct sockaddr *)&sock, socklen, buf, NI_MAXHOST, NULL, 0, 0) == 0)
			clienthost = buf;
	}

	return clienthost;
}

string SockWrapper::GetServerHostname()
{
	struct sockaddr_storage sock;
	socklen_t socklen = sizeof(sock);

	/* Get the server's hostname. */
	string serverhost = "localhost.localdomain";
	if(getsockname(recv_fd, (struct sockaddr*) &sock, &socklen) == 0)
	{
		char buf[NI_MAXHOST+1];

		if(getnameinfo((struct sockaddr *) &sock, socklen, buf, NI_MAXHOST, NULL, 0, 0 ) == 0)
			serverhost = buf;
	}

	return serverhost;
}

int SockWrapper::AttachCallback(PurpleInputCondition cond, _CallBack* cb)
{
	return glib_input_add(recv_fd, cond, g_callback_input, cb);
}

void SockWrapper::EndSessionCleanup()
{
	close(recv_fd);
	if (send_fd != recv_fd)
		close(send_fd);
}

