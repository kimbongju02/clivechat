#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <algorithm>
#include <arpa/inet.h>
#include <fstream>

using namespace std;

constexpr int PORT = 8080;
constexpr int MAX_EVENTS = 10;
constexpr int buffer_size = 256;
FILE *file = NULL;

struct Client {
    int socket;
    string username;
    string room;
};

int serverSocket;
vector<Client> clients;

void handleNewConnection(int);
void handleClientData(int, int);
void send_file(int, auto);

// 새로운 클라이언트가 서버에 연결되면 호출
void handleNewConnection(int epoll_fd) {
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);
    // accept 함수로 대기 중인 연결 수락, 클라이언트와 통신위한 소켓 반환
    int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientSocket < 0) { // 연결 실패
        cerr << "Error accepting connection\n";
        return;
    }

    // 새로운 클라이언트의 IP 주소와 포트 출력
    cout << "New connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "\n";

    // 클라이언트 소켓을 논블로킹 모드로 설정
    int flags = fcntl(clientSocket, F_GETFL, 0);
    flags &= ~O_NONBLOCK;
    fcntl(clientSocket, F_SETFL, flags);

    // 이벤트를 감지할 소켓 등록
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = clientSocket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientSocket, &event);

    // 새로운 클라이언트 객체 생성 및 벡터에 추가
    clients.push_back({clientSocket, "", ""});

    // 사용자 등록 및 채팅방 선택을 위한 환영 메시지 전송
    const char* welcomeMessage = "Welcome! Please enter your username and chat room (e.g., username room_name):\n";
    send(clientSocket, welcomeMessage, strlen(welcomeMessage), 0);
}


void handleClientData(int clientSocket, int epoll_fd) {
    char buffer[buffer_size];
    // 클라이언트로부터 데이터 수신
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

    // 클라이언트 연결 해제
    if (bytesRead <= 0) {
        auto client = find_if(clients.begin(), clients.end(),[clientSocket](const Client& c) { return c.socket == clientSocket; });
        cout << "Client disconnected. Socket: " << clientSocket << " Username: " << client->username << " Room: " << client->room << "\n";
        // epoll 세트에서 클라이언트 소켓 제거 및 소켓 닫기
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, clientSocket, nullptr);
        close(clientSocket);

        // 벡터에서 클라이언트 제거
        auto it = remove_if(clients.begin(), clients.end(),[clientSocket](const Client& c) { return c.socket == clientSocket; });
        if (it != clients.end()) {
            clients.erase(it, clients.end());
        }
    } else {
        buffer[bytesRead] = '\0';
        string message(buffer);

        // 클라이언트의 사용자 이름, 채팅방이 비어있으면 사용자 등록 및 채팅방 선택
        auto client = find_if(clients.begin(), clients.end(),
                            [clientSocket](const Client& c) { return c.socket == clientSocket; });
        if (client != clients.end() && client->username.empty()) {
            // 사용자 등록 및 채팅방 선택
            size_t spacePos = message.find(' ');
            if (spacePos != string::npos) {
                client->username = message.substr(0, spacePos);
                client->room = message.substr(spacePos + 1);
                cout << "User registered: " << client->username << " in room: " << client->room << "\n";

                // 환영 메시지 전송
                string welcomeMessage = "Welcome to the chat room, " + client->username + "!\n";
                send(clientSocket, welcomeMessage.c_str(), welcomeMessage.length(), 0);
            } else {
                // 잘못된 메시지 형식
                const char* errorMessage = "Invalid message format. Please enter your username and chat room (e.g., username room_name):\n";
                send(clientSocket, errorMessage, strlen(errorMessage), 0);
            }
        } else {
            if (memcmp(buffer, "FILE", 4) == 0) {
                send_file(clientSocket, client);
            } else {
                // 채팅 메시지 브로드캐스트
                message = "[" + client->room + "] " + client->username + ": " + message;
                cout<<"message     "<<message<<endl;
                for (const auto& c : clients) {
                    // 같은 채팅방에 속한 클라이언트에게만 메시지 전송
                    if (c.room == client->room) {
                        send(c.socket, message.c_str(), buffer_size, 0);
                    }
                }
            }
        }
    }
}

void send_file(int clientSocket, auto client){
    char buf[buffer_size];
    char filename[buffer_size];
    char char_filesize[buffer_size];
    int filesize, fpsize;

    recv(clientSocket, filename, buffer_size, 0);
    recv(clientSocket, char_filesize, buffer_size, 0);
    filesize = stoi(char_filesize);

    string path = "./server_folder/" + string(filename);  // 경로 구분자 추가
    FILE* file = fopen(path.c_str(), "wb");
    int nbyte = 0;
    
    string send_message = "\nsend file name : " + string(filename) + "\tuser : " +client->username;
    for (const auto& c : clients) {
        // 같은 채팅방에 속한 클라이언트에게만 메시지 전송
            if (c.room == client->room) {
                int get_socket = c.socket;
                send(get_socket, send_message.c_str(), buffer_size, 0);
                send(get_socket, "FILE", buffer_size, 0);
                send(get_socket, filename, buffer_size, 0);
                send(get_socket, char_filesize, buffer_size, 0);
            }
        }
    while (nbyte!=filesize) {
        fpsize = recv(clientSocket, buf, buffer_size, 0);
        nbyte += fpsize;
        for (const auto& c : clients) {
        // 같은 채팅방에 속한 클라이언트에게만 메시지 전송
            if (c.room == client->room) {
                int get_socket = c.socket;
                send(get_socket, buf, fpsize, 0);
            }
        }
        memset(buf, 0, buffer_size);
    }
}

int main(int argc, char* argv[]) {
    // 서버 소켓 생성
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        cerr << "Error creating socket\n";
        return -1;
    }
    
    // 서버의 주소와 포트 생성
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    // bind 함수로 소켓에 주소 할당
    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        cerr << "Error binding socket\n";
        close(serverSocket);
        return -1;
    }

    // listen 함수로 클라이언트 연결 수신 대기 상태
    if (listen(serverSocket, 5) < 0) {
        cerr << "Error listening on socket\n";
        close(serverSocket);
        return -1;
    }

    cout << "Server is listening on port " << PORT << "...\n";

    // epoll 인스턴스 생성
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        cerr << "Error creating epoll instance\n";
        close(serverSocket);
        return -1;
    }

    // epoll_event 구조체 사용하여 서버 소켓에 대한 이벤트 설정
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = serverSocket;
    //epoll_ctl 함수로 서버 소켓을 epoll 세트에 추가
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, serverSocket, &event) == -1) {
        cerr << "Error adding server socket to epoll set\n";
        close(serverSocket);
        close(epoll_fd);
        return -1;
    }

    epoll_event events[MAX_EVENTS];

    while (true) {
        int numEvents = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < numEvents; ++i) {
            if (events[i].data.fd == serverSocket) {
                // 서버 소켓이 준비되고, 새로운 클라이언트 등장
                handleNewConnection(epoll_fd);
            } else {
                // 클라이언트 소켓이 데이터 저장 준비 완료
                handleClientData(events[i].data.fd, epoll_fd);
            }
        }
    }

    close(serverSocket);
    close(epoll_fd);

    return 0;
}

