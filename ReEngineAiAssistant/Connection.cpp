#include "Connection.h"

void PyConnection::ConnectionInit()
{
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(65432);

    if (InetPton(AF_INET, TEXT("127.0.0.1"), &server.sin_addr) != 1) {
        std::cerr << "Invalid address or not supported\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    std::cout << "Connecting to Python...\n";
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        std::cerr << "Connection failed\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    std::cout << "Connected to Python! You can now chat.\n";

    // Start receiver thread
    receiver = std::thread(&PyConnection::receiveMessages, this);
    receiver.detach();
}

void PyConnection::receiveMessages()
{
    char buffer[2048];
    while (true) {
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
            break;
        buffer[bytes] = '\0';

        std::lock_guard<std::mutex> lock(msgMutex);
        messageQueue.push(std::string(buffer));
    }
}

void PyConnection::SendMessage(const std::string& msg)
{
    if (sock != INVALID_SOCKET && !msg.empty())
        send(sock, msg.c_str(), static_cast<int>(msg.size()), 0);
}

bool PyConnection::HasNewMessage()
{
    std::lock_guard<std::mutex> lock(msgMutex);
    return !messageQueue.empty();
}

std::string PyConnection::PopMessage()
{
    std::lock_guard<std::mutex> lock(msgMutex);
    if (messageQueue.empty()) return "";
    std::string msg = messageQueue.front();
    messageQueue.pop();
    return msg;
}
