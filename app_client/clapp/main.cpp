#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

const int maxMessageLenght = 1024;

void error(const char *msg, int code)
{
    perror(msg);
    exit(code);
}

std::string usage(const char *appname)
{
    std::string s = "Usage:\n";
    s.append(appname);
    s.append(" <host-address> <port> <tcp|udp>\n");
    return s;
}

void sendingInputMessages(int socket_fd, sockaddr_in addr, std::string mode)
{
    std::string input_string;
    while (true)
    {
        std::cout << "Input message to server: ";
        std::getline(std::cin, input_string);
        if (input_string == "QUIT") break;

        if (input_string.size() >= maxMessageLenght)
        {
            input_string.erase(maxMessageLenght);
        }

        char buff[maxMessageLenght];
        int sz = (input_string.size() < maxMessageLenght ?
                      input_string.size() : (maxMessageLenght-1)) + 1;

        if (mode == "tcp")
        {
            int n = send(socket_fd, input_string.c_str(), sz, 0);
            if (n < 0) error("Error send via socket TCP", 6);
            n = recv(socket_fd, &buff, maxMessageLenght, 0);
            if (n < 0) error("Error recv via socket TCP", 7);
            std::string answer(buff);
            std::cout << answer;
        }
        else //udp
        {
            int n = sendto(socket_fd, input_string.c_str(), sz, 0,
                   (sockaddr*)&addr, sizeof(addr));
            if (n < 0) error("Error sendto via socket UDP", 8);
            unsigned int sz_addr = sizeof (addr);
            n = recvfrom(socket_fd, &buff, maxMessageLenght, 0, (struct sockaddr *) &addr, &sz_addr);
            if (n < 0) error("Error recvfrom via socket UDP", 9);
            std::string answer(buff);
            std::cout << answer;
        }

        std::cout << '\n';
    }
    std::cout << '\n';
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::string info = "Wrong parameters count.\n";
        info.append(usage(argv[0]));
        error(info.c_str(), 1);
    }

    std::string port_s(argv[2]);
    int port_number;
    try
    {
        port_number = std::stoi(port_s);
    }
    catch (...)
    {
        std::string info = "Second parameter is not a number (port).\n";
        info.append(usage(argv[0]));
        error(info.c_str(), 2);
    }

    std::string mode_protocol(argv[3]);
    if (mode_protocol != "tcp" && mode_protocol != "udp")
    {
        std::string info = "Wrong third parameter: must be tcp or udp.\n";
        info.append(usage(argv[0]));
        error(info.c_str(), 3);
    }

    int socket_fd, result;
    sockaddr_in serv_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    hostent *server = gethostbyname(argv[1]);
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(port_number);

    if (mode_protocol == "tcp")
    {
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) error("Socket error", 4);

        result = connect(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        if (result < 0) error("Connect error", 5);

        sendingInputMessages(socket_fd, serv_addr, mode_protocol);
    }
    else // udp
    {
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd < 0) error("Socket error", 4);

        sendingInputMessages(socket_fd, serv_addr, mode_protocol);
    }

    close(socket_fd);

    return 0;
}
