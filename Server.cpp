#include "Server.hpp"

void Server::create( void ) {
    struct protoent	*pe;
    struct pollfd   newPoll;
    int             newSrvSock;

    pe = getprotobyname("tcp");
    if ((newSrvSock = socket(AF_INET, SOCK_STREAM, pe->p_proto)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    int enable = 1;
    if (setsockopt(newSrvSock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }
    newPoll.fd = newSrvSock;
    newPoll.events = POLLIN;
    newPoll.revents = 0;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(conf.hostname.c_str());
    address.sin_port = htons(atoi(conf.port.c_str()));
    if (bind(newSrvSock, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(newSrvSock, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    fcntl(newSrvSock, F_SETFL, O_NONBLOCK);
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
    /* 
    / F_SETFL устанавливет флаг O_NONBLOCK для подаваемого дескриптора 
    / O_NONBLOCK устанавливает  режим  неблокирования, 
    / что позволяет при ошибке вернуть чтение другим запросам 
    */
    fds.push_back(newPoll);
    mess.push_back("");
    cnct.push_back(false);
    srvSockets.insert(newSrvSock);
}

void Server::run( void ) {
    std::cout << GREEN << "Server running." << RESET << "\n";
    while(status & WORKING) {
        clientRequest();
        consoleCommands();
    }
    if (status & RESTART) {
        status = WORKING;
        run();
    }
}

void Server::consoleCommands( void ) {
    char buf[BUF_SIZE + 1];
    int bytesRead = 0;
    int rd;
    std::string text;
    while ((rd = read(fileno(stdin), buf, BUF_SIZE)) > 0)
    {
        buf[rd] = 0;
        text += buf;
        bytesRead += rd;
        // std::cout << RED << "console command: " << RESET << text;
        if (text.find("\n") != std::string::npos) {
            text.erase(text.find("\n"), 1); 
            break;
        }
    }
    if (bytesRead > 0)
    {
        if (text == "STOP")
        {
            std::cout << YELLOW << "Shutdown server\n" << RESET;
            closeServer(STOP);
        }
        else if (text == "RESTART")
        {
            std::cout << YELLOW << "Restarting server ... " << RESET;
            closeServer(RESTART);
            create();
        }
        else if (text == "HELP")
        {
            std::cout << YELLOW << "Allowed command: " << RESET << "\n\n";
            std::cout << " * STOP - shutdown server." << "\n\n";
            std::cout << " * RESTART - restarting server." << "\n\n";
        }
        else
        {
            std::cout << RED << "Uncnown command. Use " << RESET <<\
             "HELP" << RED << " for more information." << RESET << "\n";
        }
    }
}

void Server::disconnectClients( const size_t id ) {
    std::cout << "Client " << fds[id].fd << " disconnected." << "\n";
    close(fds[id].fd);

    fds.erase(fds.begin() + id);
    mess.erase(mess.begin() + id);
    cnct.erase(cnct.begin() + id);
}

void Server::connectClients( const int & fd ) {
    int newClientSock;
    struct sockaddr_in clientaddr;
    int addrlen = sizeof(clientaddr);

    if ((newClientSock = accept(fd, (struct sockaddr*)&clientaddr, (socklen_t*)&addrlen)) > 0) {
        struct pollfd nw;

        nw.fd = newClientSock;
        nw.events = POLLIN;
        nw.revents = 0;
        fds.push_back(nw);
        mess.push_back("");
        cnct.push_back(false);
        std::cout << "New client on " << newClientSock << " socket." << "\n";
    }
}

void Server::clientRequest( void ) {
    int ret = poll(fds.data(), fds.size(), 0);
    if (ret != 0)    {
        for (size_t id = 0; id < fds.size(); id++) {
            if (fds[id].revents & POLLIN) {
                if (isServerSocket(fds[id].fd))
                    connectClients(fds[id].fd);
                else if (readRequest(id) <= 0)
                    disconnectClients(id);
                else if (!cnct[id]) {
                    // REQUEST PART
                    req.parseText(mess[id]);


                    //  RESPONSE PART
                    if (mess[id].size())
                        std::cout << YELLOW << "Client " << fds[id].fd << " send (full message): " << RESET << mess[id];
                        
						make_response(req, id);
                    // }
                    mess[id] = "";
                }
                fds[id].revents = 0;
            }
        }
    }
}

static bool checkConnection( const std::string & mess ) {
    if (mess.find_last_of("\n") != mess.size() - 1)
        return true;
    return false;
}

int  Server::readRequest( const size_t id ) {
    char buf[BUF_SIZE + 1];
    int bytesRead = 0;
    int rd;
    std::string text;
    if (mess[id].size() > 0)
		text = mess[id];
    while ((rd = recv(fds[id].fd, buf, BUF_SIZE, 0)) > 0) {
        buf[rd] = 0;
        bytesRead += rd;
        text += buf;
        if (text.find("\n") != std::string::npos)
            break;
    }
    while (text.find("\r") != std::string::npos)      // Удаляем символ возврата карретки
        text.erase(text.find("\r"), 1);               // из комбинации CRLF
    if (text.size() > BUF_SIZE)   //Длина запроса Не более 2048 символов
    {
        text.replace(BUF_SIZE - 2, 2, "\r\n");
        std::cout << RED << "ALERT! text more than 512 bytes!" << RESET << "\n";
    }
    cnct[id] = checkConnection(text);
    mess[id] = text;
    return (bytesRead);
}

void    Server::closeServer( int new_status ) {

    if (conf.error_fd > 2 && new_status & STOP)
        close(conf.error_fd);
    if (conf.access_fd > 2 && new_status & STOP)
        close(conf.access_fd);
    size_t count = fds.size();
    for (size_t i = 0; i < count; i++)
        close(fds[i].fd);
    fds.clear();
    this->status = new_status;
}

bool    Server::isServerSocket( const int & fd ) {
    if (srvSockets.find(fd) != srvSockets.end())
        return true;
    return false;
}

void    Server::writeLog( int dest, const std::string & header, const std::string & text ) {
    int fd;
    if (this->flags & ERR_LOG && dest & ERR_LOG)
        fd = conf.error_fd;
    else if (this->flags & ACS_LOG && dest & ACS_LOG)
        fd = conf.access_fd;
    else
        fd = 0;
    if (fd) {
        std::time_t result = std::time(nullptr);
        std::string time = std::asctime(std::localtime(&result));
        time = "[" + time.erase(time.size() - 1) + "] ";
        write(fd, time.c_str(), time.size());
        write(fd, header.c_str(), header.size());
        write(fd, "\n\n", 2);
        write(fd, text.c_str(), text.size());
        write(fd, "\n\n", 2);
    }
}

void	Server::errorShutdown( int code, const std::string & error, const std::string & text ) {
    writeLog(ERR_LOG, error, text);
    if (this->flags & ERR_LOG)
        std::cerr << "error: see error.log for more information\n";
    closeServer(STOP);
    exit(code);
}

Server::Server( const int & config_fd ) {

    // parseConfig(config_fd);
    // std::cout << conf.hostname << ":" << conf.port << "\n";

    status = WORKING;
    flags = 0;
}

Server::~Server() {
    std::cout << "Destroyed.\n";
}
