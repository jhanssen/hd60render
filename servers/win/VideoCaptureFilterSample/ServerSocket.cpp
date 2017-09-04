#include "stdafx.h"
#include "ServerSocket.h"
#include <mutex>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#pragma comment (lib, "Ws2_32.lib")

std::once_flag initOnce;

ServerSocket::ServerSocket(uint16_t port)
	: mSocket(INVALID_SOCKET), mStopped(false)
{
	std::call_once(initOnce, []() {
		WSADATA wsaData;
		WSAStartup(MAKEWORD(2, 2), &wsaData);
	});

	char strport[6];
	snprintf(strport, sizeof(strport), "%u", port);

	int e;
	struct addrinfo *result = nullptr;
	struct addrinfo hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	e = getaddrinfo(nullptr, strport, &hints, &result);
	if (e != 0) {
		// bad
		//WSACleanup();
		return;
	}

	mSocket = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (mSocket == INVALID_SOCKET) {
		// also bad
		freeaddrinfo(result);
		//WSACleanup();
		return;
	}

	e = bind(mSocket, result->ai_addr, (int)result->ai_addrlen);
	if (e == SOCKET_ERROR) {
		// yep, bad
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		freeaddrinfo(result);
		//WSACleanup();
		return;
	}

	freeaddrinfo(result);

	e = listen(mSocket, SOMAXCONN);
	if (e == SOCKET_ERROR) {
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		//WSACleanup();
		return;
	}

	mWakeup = WSACreateEvent();
	if (mWakeup == WSA_INVALID_EVENT) {
		// ahm
		closesocket(mSocket);
		mSocket = INVALID_SOCKET;
		//WSACleanup();
		return;
	}

	// make our thread
	mThread = std::thread(thread, this);
}

ServerSocket::~ServerSocket()
{
	stop();
}

void ServerSocket::thread(ServerSocket* server)
{
	enum { MaxThreshold = 30 * 1024 * 1024, MinThreshold = 20 * 1024 * 1024 };

	auto serverEvent = WSACreateEvent();
	if (serverEvent == WSA_INVALID_EVENT) {
		// woops
		return;
	}

	auto nonblock = [](SOCKET socket) {
		unsigned long mode = 1;
		ioctlsocket(socket, FIONBIO, &mode);
	};

	nonblock(server->mSocket);

	std::vector<WSAEVENT> events;
	std::vector<SOCKET> sockets;

	// put in our wakeup event
	events.push_back(server->mWakeup);
	sockets.push_back(0);

	auto handleWrite = [server, &sockets]() {
		std::unique_lock<std::mutex> locker(server->mMutex);
		// drop everything if we have no clients
		if (sockets.size() == 2) { // 2, one is our dummy socket and one is our server socket
			server->mBuffer.bytes.clear();
			server->mBuffer.offset = 0;
			server->mSocketBuffers.clear();
		}

		// early return if we have nothing to write
		if (server->mBuffer.isEmpty() && server->mSocketBuffers.empty()) {
			return;
		}
		auto write = [](SOCKET socket, const ServerSocket::Buffer& buffer) -> int {
			// write as much as possible
			int accum = 0;
			for (;;) {
				_ASSERT(buffer.offset + accum <= buffer.bytes.size());
				if (buffer.offset + accum == buffer.bytes.size()) {
					// we're done
					return accum;
				}
				const BYTE* b = &buffer.bytes[buffer.offset + accum];
				const size_t rem = buffer.bytes.size() - (buffer.offset + accum);
				int r = ::send(socket, reinterpret_cast<const char*>(b), rem, 0);
				if (r == SOCKET_ERROR) {
					// wuuups
					int err = WSAGetLastError();
					if (err == WSAEWOULDBLOCK) {
						// this is ok
						return accum;
					}
					return -1;
				}
				_ASSERT(r > 0);
				accum += r;
				// keep going?
			}
		};
		int firstWritten = -1;
		for (size_t socki = 2; socki < sockets.size(); ++socki) {
			auto& socket = sockets[socki];
			auto local = server->mSocketBuffers.find(socket);
			if (local != server->mSocketBuffers.end()) {
				int w = write(socket, local->second);
				if (w > 0) {
					local->second.offset += w;
					if (local->second.offset == local->second.bytes.size()) {
						// we've written everything, clear the buffer
						local->second.bytes.clear();
						local->second.offset = 0;
					}
					// this is suboptimal and needs to be fixed, we should really remove the buffer if we've written everything
					// but we can't unless we know that we're in sync with the global mBuffer

					/*
					if (local->second.offset == local->second.bytes.size()) {
						// done, remove this entry from the list of buffers
						server->mSocketBuffers.erase(*socket);
					}
					*/
				} else if (w == 0 && local->second.bytes.size() > MaxThreshold) {
					// truncate to threshold
					local->second.bytes.erase(local->second.bytes.begin(), local->second.bytes.end() - MinThreshold);
				} else if (w < 0) {
					// just erase the buffer
					local->second.bytes.clear();
					local->second.offset = 0;
				}
			}
			else {
				int w = write(socket, server->mBuffer);
				if (w > 0 && firstWritten == -1) {
					firstWritten = w;
				} else if (w > 0 && firstWritten != w) {
					// we need to set up a new copy of the buffer
					ServerSocket::Buffer buf = { server->mBuffer.bytes, server->mBuffer.offset + w };
					server->mSocketBuffers[socket] = std::move(buf);
				} else if (w == 0 && server->mBuffer.bytes.size() > MaxThreshold) {
					// truncate to threshold
					server->mBuffer.bytes.erase(server->mBuffer.bytes.begin(), server->mBuffer.bytes.end() - MinThreshold);
				} else if (w < 0) {
					// just erase the buffer
					local->second.bytes.clear();
					local->second.offset = 0;
				}
			}
		}
		if (firstWritten > 0) {
			server->mBuffer.offset += firstWritten;
			// if we've written everything, clear the buffer
			if (server->mBuffer.offset == server->mBuffer.bytes.size()) {
				server->mBuffer.bytes.clear();
				server->mBuffer.offset = 0;
			}
		}
	};

	enum HandleResult { Failure, Ok, Removed };
	auto handleEvent = [server, &serverEvent, &sockets, &events, &handleWrite, &nonblock](SOCKET socket, WSAEVENT event) -> HandleResult {
		WSANETWORKEVENTS nevents;
		int ret = WSAEnumNetworkEvents(socket, event, &nevents);
		if (ret == SOCKET_ERROR) {
			// welp.
			return Failure;
		}
		if (nevents.lNetworkEvents & FD_ACCEPT) {
			if (nevents.iErrorCode[FD_ACCEPT_BIT] != 0) {
				// accept error?
				return Failure;
			}
			SOCKET client = ::accept(socket, nullptr, nullptr);
			if (client == INVALID_SOCKET) {
				// something bad just happened
				return Failure;
			}
			nonblock(client);
			auto event = WSACreateEvent();
			if (event == WSA_INVALID_EVENT) {
				// badness abound
				return Failure;
			}
			ret = WSAEventSelect(client, event, FD_READ | FD_WRITE | FD_CLOSE);
			if (ret == SOCKET_ERROR) {
				// geh
				WSACloseEvent(event);
				return Failure;
			}
			sockets.push_back(client);
			events.push_back(event);

			handleWrite();
		}
		if (nevents.lNetworkEvents & FD_READ) {
			if (nevents.iErrorCode[FD_READ_BIT] != 0) {
				// read error?
				return Failure;
			}
			// we want to read stuff
			// call recv
		}
		if (nevents.lNetworkEvents & FD_WRITE) {
			if (nevents.iErrorCode[FD_WRITE_BIT] != 0) {
				// write error?
				return Failure;
			}
			// we want to write stuff
			handleWrite();
		}
		if (nevents.lNetworkEvents & FD_CLOSE) {
			// close + take the socket and event out
			closesocket(socket);
			WSACloseEvent(event);
			auto pos = std::find(sockets.begin(), sockets.end(), socket);
			if (pos == sockets.end()) {
				// this is really bad
				return Failure;
			}
			const int off = pos - sockets.begin();
			sockets.erase(pos);
			events.erase(events.begin() + off);

			{
				std::unique_lock<std::mutex> locker(server->mMutex);
				server->mSocketBuffers.erase(socket);
			}

			handleWrite();

			return Removed;
		}
		return Ok;
	};

	int r = WSAEventSelect(server->mSocket, serverEvent, FD_ACCEPT | FD_CLOSE);
	if (r == SOCKET_ERROR) {
		// badness
		return;
	}
	DWORD idx, nidx;
	events.push_back(serverEvent);
	sockets.push_back(server->mSocket);
	for (;;) {
		{
			std::unique_lock<std::mutex> locker(server->mMutex);
			if (server->mStopped)
				return;
		}
		idx = WSAWaitForMultipleEvents(events.size(), &events[0], FALSE, 500, FALSE);
		if (idx == WSA_WAIT_TIMEOUT) {
			continue;
		}
		if (idx - WSA_WAIT_EVENT_0 == 0) {
			// our wakeup event, reset it
			WSAResetEvent(events[0]);
			// check if we have anything to write
			handleWrite();

			// continue the loop below at index 1.
			++idx;
		}
		// wow, this API sucks ass
		for (DWORD i = idx - WSA_WAIT_EVENT_0; i < events.size(); ++i) {
			nidx = WSAWaitForMultipleEvents(1, &events[i], FALSE, 0, FALSE);
			if (nidx == WSA_WAIT_TIMEOUT) {
				continue;
			} else if (nidx == WSA_WAIT_FAILED) {
				// take the socket out of our list
				// what if this is our server socket?
				{
					std::unique_lock<std::mutex> locker(server->mMutex);
					server->mSocketBuffers.erase(sockets[i]);
				}

				WSACloseEvent(events[i]);
				events.erase(events.begin() + i);
				closesocket(sockets[i]);
				sockets.erase(sockets.begin() + 1);
				--i;
			} else {
				// handle our socket
				if (handleEvent(sockets[i], events[i]) == Removed) {
					// socket was removed
					--i;
				}
			}
		}
	}
}

void ServerSocket::send(const BYTE* byte, size_t size)
{
	auto copy = [](Buffer& buf, const BYTE* byte, size_t size) {
		const size_t oldsize = buf.bytes.size();
		buf.bytes.resize(oldsize + size);
		memcpy(&buf.bytes[oldsize], byte, size);
	};
	std::unique_lock<std::mutex> locker(mMutex);
	copy(mBuffer, byte, size);
	// if we have any socket local buffers, copy to those too
	for (auto it = mSocketBuffers.begin(); it != mSocketBuffers.end(); ++it) {
		copy(it->second, byte, size);
	}
	WSASetEvent(mWakeup);
}

void ServerSocket::stop()
{
	if (mSocket == INVALID_SOCKET)
		return;
	{
		std::unique_lock<std::mutex> locker(mMutex);
		mStopped = true;
	}
	WSASetEvent(mWakeup);
	mThread.join();

	closesocket(mSocket);
	mSocket = INVALID_SOCKET;
}