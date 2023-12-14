// UI
#include <gtkmm.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <thread>
#include <fstream>
#include <string.h>

using namespace std;

constexpr int PORT = 8080;
constexpr int buffer_size = 256;
string filename, file_path;
FILE *file = NULL;

class ChatWindow : public Gtk::Window {
public:
    ChatWindow() {
        set_title("C live chat");
        set_default_size(400, 300);

        // UI 요소 초기화
        chat_view.set_editable(false);
        message_entry.set_max_length(100);

        // 레이아웃 구성
        main_box.pack_start(chat_view, Gtk::PACK_EXPAND_WIDGET);
        main_box.pack_start(message_entry, Gtk::PACK_SHRINK);
        main_box.pack_start(send_button, Gtk::PACK_SHRINK);
        main_box.pack_start(upload_button, Gtk::PACK_SHRINK);

        // 버튼 클릭 시 이벤트 핸들러 등록
        send_button.signal_clicked().connect(sigc::mem_fun(*this, &ChatWindow::on_send_button_clicked));
        upload_button.signal_clicked().connect(sigc::mem_fun(*this, &ChatWindow::on_upload_button_clicked));

        // 윈도우가 닫힐 때의 이벤트 핸들러 등록
        signal_hide().connect(sigc::mem_fun(*this, &ChatWindow::close_window));

        // 서버 연결 및 채팅방 접속
        server_connect();
        room_access();

        receive_thread = thread(&ChatWindow::handleClientData, this);

        // 메인 루프 시작
        add(main_box);
        show_all_children();
    }

    void server_connect() {
        // 서버와 연결
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == -1) {
            cerr << "Error creating socket\n";
            return;
        }
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(PORT);
        inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
            cerr << "Error connecting to server\n";
            ::close(clientSocket);
            return;
        }
        cout << clientSocket << "\n";
    }

    void handleClientData() {
        char buffer[buffer_size];
        int bytesRead;

        while (true) {
            bytesRead = recv(clientSocket, buffer, buffer_size, 0);

            if (memcmp(buffer, "FILE", 4) == 0) {
               get_file();
            }else{
                if (bytesRead == -1) {
                    // 오류 처리
                    cerr << "Error receiving data\n";
                    break;
                } else if (bytesRead == 0) {
                    // 연결이 끊겼음을 나타냄
                    cerr << "Connection closed by the server\n";
                    break;
                }

                buffer[bytesRead] = '\0';

                if (bytesRead > 0) {
                    Glib::signal_idle().connect_once([this, message = string(buffer)] {
                        chat_view.get_buffer()->insert_at_cursor(message + "\n");
                        message_entry.set_text("");
                    });
                }
            }
        }
    }

    void get_file(){
        char buf[buffer_size];
        char filename[buffer_size];
        char char_filesize[buffer_size];
        int filesize, fpsize;

        recv(clientSocket, filename, buffer_size, 0);
        recv(clientSocket, char_filesize, buffer_size, 0);
        filesize = stoi(char_filesize);

        string folder_path = "./"+user_name+"/";
        string path = folder_path + string(filename);  // 경로 구분자 추가
        mkdir(folder_path.c_str(), 0777);
        FILE* file = fopen(path.c_str(), "wb");

        int nbyte = 0;
        while (nbyte!=filesize) {
            fpsize = recv(clientSocket, buf, buffer_size, 0);
            nbyte += fpsize;
            fwrite(buf, 1, fpsize, file);
            memset(buf, 0, buffer_size);
        }
        fclose(file);
    }

    void room_access() {

        Gtk::MessageDialog dialog(*this, "Enter Your Information", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_OK_CANCEL);
        dialog.set_secondary_text("Please enter your name and room number:");

        Gtk::Label label_name("Name:");
        Gtk::Entry entry_name;
        entry_name.set_max_length(15);
        entry_name.set_text(user_name);
        entry_name.show();
        dialog.get_content_area()->pack_start(label_name, Gtk::PACK_SHRINK);
        dialog.get_content_area()->pack_end(entry_name, Gtk::PACK_SHRINK);

        Gtk::Label label_room("Room Number:");
        Gtk::Entry entry_room;
        entry_room.set_max_length(10);
        entry_room.set_text(room_number);
        entry_room.show();
        dialog.get_content_area()->pack_start(label_room, Gtk::PACK_SHRINK);
        dialog.get_content_area()->pack_end(entry_room, Gtk::PACK_SHRINK);

        int result = dialog.run();
        if (result == Gtk::RESPONSE_OK) {
            room_number  = entry_name.get_text();
            user_name = entry_room.get_text();
        } else {
            hide();
        }

        // Example: You need to replace this with your own data, size, and flags
        string data= user_name + " "+ room_number;
        send(clientSocket,data.c_str(),data.length(),0);
    }
    

    void on_send_button_clicked() {
        string message = message_entry.get_text();
        if (!message.empty() && filename.empty()) {
            ssize_t sentBytes = send(clientSocket, message.c_str(), message.length() + 1, 0);

            if (sentBytes == -1) {
                // 에러 처리
                cerr << "Error sending data\n";
            }

            message_entry.set_text("");
        }else if(!message.empty() && !filename.empty()) {
            ssize_t sentBytes = send(clientSocket, message.c_str(), message.length() + 1, 0);
            if (sentBytes == -1) {
                // 에러 처리
                cerr << "Error sending data\n";
            }
            send_file();
        }
        else if(message.empty() && !filename.empty()) {
            send_file();
        }
    }

    // 파일 전송
    void send_file(){
        file = fopen(file_path.c_str(), "rb");
        if (!file) {
            cerr << "Error opening file: " << file_path << endl;
            // 적절한 에러 처리를 수행하세요.
            return; // 예를 들어, 함수를 종료하거나 오류 메시지를 출력한 후 리턴합니다.
        }
                    
        // move file pointer to end
        fseek(file, 0, SEEK_END);
        // calculate file size
        size_t fsize=ftell(file);
        // move file pointer to first
        fseek(file, 0, SEEK_SET);

        char file_buf[buffer_size];   // 256 바이트 버퍼

        send(clientSocket, "FILE", strlen("FILE"), 0);
        send(clientSocket, filename.c_str(), buffer_size, 0);
        send(clientSocket, to_string(fsize).c_str(), buffer_size, 0);

        size_t nsize = 0;
        while (nsize!=fsize) {
            // read from file to buf
            // 1byte * 256 count = 256byte => buf[256];
            int fpsize = fread(file_buf, 1, buffer_size, file);
            nsize+=fpsize;
            send(clientSocket, file_buf, fpsize, 0);
            memset(file_buf, 0, sizeof(file_buf));
        }

        fclose(file);
        // filename 초기화
        filename.clear();
        file_path.clear();
    }

    void close_window() {
        cout << "Chat window closed.\n";
        // 소켓 닫기
        if (clientSocket != -1) {
            ::close(clientSocket);
            clientSocket = -1; // 소켓을 닫은 후에는 -1로 초기화
        }
    }

    void on_upload_button_clicked() {
        // 파일 업로드 다이얼로그 표시
        Gtk::FileChooserDialog file_dialog("Please choose a file",
                                          Gtk::FILE_CHOOSER_ACTION_OPEN);
        file_dialog.set_transient_for(*this);
        file_dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
        file_dialog.add_button("_Open", Gtk::RESPONSE_OK);

        // 파일 다이얼로그를 통해 파일 선택
        int result = file_dialog.run();
        if (result == Gtk::RESPONSE_OK) {
            file_path = file_dialog.get_filename();
            filename = Glib::path_get_basename(file_path);
        }
    }

private:
    Gtk::VBox main_box;
    Gtk::TextView chat_view;
    Gtk::Entry user_entry;
    Gtk::Entry message_entry;
    Gtk::Button send_button{"Send"};
    Gtk::Button upload_button{"Upload File"};

    thread receive_thread;
    string user_name;
    string room_number;
    int clientSocket;
};

int main(int argc, char* argv[]) {

    Glib::RefPtr<Gtk::Application> app = Gtk::Application::create(argc, argv, "org.gtkmm.example", Gio::APPLICATION_NON_UNIQUE);

    ChatWindow chat_window1;

    chat_window1.set_position(Gtk::WIN_POS_CENTER);

    app->run(chat_window1);

    return 0;
}
