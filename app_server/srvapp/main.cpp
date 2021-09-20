#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <termios.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <unordered_set>

std::atomic<bool> finish_escPressed;

const int maxMessageLenght = 1024;

void error(const char *msg, int code)
{
    perror(msg);
    exit(code);
}

int kb_chk_esc_key()
{
    struct termios old_term_attr; // обычные атрибуты терминала
    struct termios new_term_attr; // атрибуты терминала, которые будут изменены

    tcgetattr(STDIN_FILENO, &old_term_attr); // сохранение обычных атрибутов терминала для STDIN
    new_term_attr = old_term_attr;
    // присовение атрибутов: не-эхо (ввод не отображается) и не-канонический (ввод не требует Enter):
    new_term_attr.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term_attr); // применение новых атрибутов

    int old_file_status = fcntl(STDIN_FILENO, F_GETFL, 0); // флаги статуса STDIN - в old_file_status
    // установка прежних флагов статуса + флаг неблокирующей операции
    fcntl(STDIN_FILENO, F_SETFL, old_file_status | O_NONBLOCK);

    int ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term_attr); // восстановление обычных атрибутов
    fcntl(STDIN_FILENO, F_SETFL, old_file_status); // восстановление состояния флагов

    if (ch == '\e') // перехват нажатия Escape (gcc)
    {
        finish_escPressed = true;
        return 1;
    }
    return 0;
}

std::string processedMessage(std::string in_message)
{
    std::string answer;
    std::istringstream fst(in_message);
    std::string s;
    auto summ = 0ULL, v = 0ULL;
    std::vector<int> values;
    while (std::getline(fst, s, ' '))
    {
        auto ok_number = true;
        auto sz = s.size();
        if (s[0] == '-') s.erase(s.begin()); // минус впереди
        if (s[sz-1] == '.' || s[sz-1] == ',') s.erase(sz-1); // точка или запятая в конце

        if (s.empty()) continue;

        auto only_digits = [](const std::string& s)
        {
            return std::count_if(s.begin(), s.end(),
                                 [](unsigned char c){ return std::isdigit(c); }
                                )
                    == (int)s.size();
        };

        ok_number = only_digits(s);

        if (ok_number)
        {
            v = std::stoull(s);
            values.push_back(v);
            summ += v;
        }
    }
    std::sort(values.begin(), values.end());
    if (values.size())
    {
        for (auto val : values)
        {
            answer.push_back(' ');
            answer.push_back(val);
        }
        answer.erase(0, 1); // удаление первого пробела
    }

    answer.push_back('\n');
    answer.append(std::to_string(summ));
    return answer;
}

void udp_work(int port_number, int maxLenMsg)
{
    const int maxMsgLen = maxLenMsg;
    int sockfd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_udp < 0)
       error("ERROR opening UDP socket", 3);

    fcntl(sockfd_udp, F_SETFL, O_NONBLOCK); // перевод файла сокета в неблокирующий режим

    struct sockaddr_in serv_addr, cli_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_number);

    if (bind(sockfd_udp, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
             error("ERROR on binding UDP", 4);

    char buffer[maxMsgLen];

    while (!finish_escPressed)
    {
        memset((char *)&buffer, 0, maxLenMsg);
        unsigned int sz_ = sizeof(cli_addr);
        int n = recvfrom(sockfd_udp, buffer, maxLenMsg-1, 0,
                         (struct sockaddr *) &cli_addr, &sz_);
        if (n <= 0) continue;
        std::string msg_in(buffer);
        auto res = processedMessage(msg_in);
        int sz = ((int)res.size() < maxLenMsg ?
                      res.size() : (maxLenMsg-1)) + 1;
        n = sendto(sockfd_udp, res.c_str(), sz, 0,
                   (struct sockaddr *) &cli_addr, sz_);
    }

    close(sockfd_udp);
}

void tcp_work(int port_number, int maxLenMsg)
{
    const int maxMsgLen = maxLenMsg;
    char buffer[maxMsgLen];

    int sockfd_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_tcp < 0)
       error("ERROR opening TCP socket", 5);

    fcntl(sockfd_tcp, F_SETFL, O_NONBLOCK);

    struct sockaddr_in serv_addr; //, cli_addr;
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_number);

    if (bind(sockfd_tcp, (struct sockaddr *) &serv_addr,
             sizeof(serv_addr)) < 0)
             error("ERROR on binding TCP", 6);

    listen(sockfd_tcp, 2);

    std::unordered_set<int> tcp_clients_other;

    while (!finish_escPressed)
    {
        fd_set set_clients;
        FD_ZERO(&set_clients);

        timeval time_out;
        time_out.tv_sec = 2;
        time_out.tv_usec = 0;

        FD_SET(sockfd_tcp, &set_clients);

        for (auto it = tcp_clients_other.begin(); it != tcp_clients_other.end(); ++it)
        {
            FD_SET(*it, &set_clients);
        }

        int max_all = sockfd_tcp;
        if (!tcp_clients_other.empty())
        {
            int max_cl_fd_other = *std::max_element(tcp_clients_other.begin(), tcp_clients_other.end());
            max_all = std::max(sockfd_tcp, max_cl_fd_other);
        }

        int res_sel = select(max_all+1, &set_clients, nullptr, nullptr, &time_out);
        if (res_sel <= 0)
        {
            continue; //error("select", 7);
        }

        if (FD_ISSET(sockfd_tcp, &set_clients))
        {
            int new_sockfd = accept(sockfd_tcp, nullptr, nullptr);
            if (new_sockfd < 0) error("Invalid TCP socket accepted", 8);
            fcntl(new_sockfd, F_SETFL, O_NONBLOCK);
            tcp_clients_other.insert(new_sockfd);
        }

        for (auto it = tcp_clients_other.begin(); it != tcp_clients_other.end(); ++it)
        {
            if (FD_ISSET(*it, &set_clients))
            {
                memset((char *)&buffer, 0, maxMsgLen);
                int read_bytes = recv(*it, buffer, maxMsgLen-1, 0);
                if (read_bytes <= 0)
                {
                    close(*it);
                    tcp_clients_other.erase(*it);
                }
                else
                {
                    std::string msg_in(buffer);
                    auto res = processedMessage(msg_in);
                    int sz = ((int)res.size() < maxLenMsg ?
                                  res.size() : (maxLenMsg-1)) + 1;
                    send(*it, res.c_str(), sz, 0);
                }
            }
        }
    }

    while (tcp_clients_other.size())
    {
        auto it_first = tcp_clients_other.begin();
        close(*it_first);
        tcp_clients_other.erase(*it_first);
    }

    close(sockfd_tcp);
}

int main(int argc, char** argv)
{
    finish_escPressed = false;

    if (argc != 3)
    {
        std::cout << "usage " << argv[0] << " udp-port tcp-port\n";
        return 1;
    }

    int port_num_udp = 0, port_num_tcp = 0;

    std::string s_port_number_udp(argv[1]), s_port_number_tcp(argv[2]);
    try
    {
        port_num_udp = std::stoi(s_port_number_udp);
        port_num_tcp = std::stoi(s_port_number_tcp);
    }
    catch (...)
    {
        std::cout << "parameters must be integer numbers\n" <<
                             "usage " << argv[0] << " udp-port tcp-port\n";
        return 2;
    }

    //

    std::thread th_udp { udp_work, port_num_udp, maxMessageLenght };
    std::thread th_tcp { tcp_work, port_num_tcp, maxMessageLenght };

    while (!finish_escPressed)
    {
        kb_chk_esc_key(); // проверка нажатия клавиши Esc каждые 300 миллисекунд
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    th_udp.join();
    th_tcp.join();

    return 0;
}
