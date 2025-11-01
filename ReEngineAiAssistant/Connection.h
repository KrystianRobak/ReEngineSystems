#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

class PyConnection
{
public:
    void ConnectionInit();
    void receiveMessages();
    void SendMessage(const std::string& msg);
    bool HasNewMessage();
    std::string PopMessage();

private:
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;

    std::thread receiver;
    std::mutex msgMutex;
    std::queue<std::string> messageQueue;
};
