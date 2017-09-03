#pragma once

#include <cstdint>
#include <windows.h>
#include <winsock2.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

class ServerSocket
{
public:
	ServerSocket(uint16_t port);
	~ServerSocket();

	bool isValid() const { return mSocket != INVALID_SOCKET; }

	void stop();
	void send(const BYTE* buffer, size_t size);

private:
	static void thread(ServerSocket* socket);

private:
	SOCKET mSocket;
	WSAEVENT mWakeup;
	std::vector<SOCKET> mClients;
	std::thread mThread;
	std::mutex mMutex;
	//std::condition_variable mCond;
	bool mStopped;

	struct Buffer
	{
		Buffer() : offset(0) {}
		Buffer(const std::vector<BYTE>& b, size_t o) : bytes(b), offset(o) {}
		bool isEmpty() const { return bytes.empty(); }

		std::vector<BYTE> bytes;
		size_t offset;
	};
	Buffer mBuffer;

	std::unordered_map<SOCKET, Buffer> mSocketBuffers;
};