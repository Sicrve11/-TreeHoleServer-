/* @Author shigw    @Email sicrve@gmail.com */

#include "TreeHoleClient.h"
#include <string>


// 设置套接字为非阻塞
int TreeHoleClient::setSocketNonBlocking1(int fd) {
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag == -1) return -1;

    flag |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flag) == -1) return -1;
    return 0;
}

TreeHoleClient::TreeHoleClient(const string& ip, int port, const string& user)
   :servIP_(ip), servPort_(port), username_(user) {

    // 清空缓存
    memset(buff_, 0, sizeof(buff_)/sizeof(char));

    // 连接远程服务器
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(servPort_);
    const char* servIP_in = servIP_.c_str();
    inet_pton(AF_INET, servIP_in, &servaddr.sin_addr);

    
    sockfd_ = socket(PF_INET, SOCK_STREAM, 0);
    if (connect(sockfd_, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("err2");
    }
    setSocketNonBlocking1(sockfd_);
    
    cout << " ***** 服务器连接成功！欢迎 " << username_ << " 的访问！ ***** " << endl;
    // 发"GET  HTTP/1.1"
    // 发送hello，获取index.html
    string p = "GET / HTTP/1.1\r\nHost: " + servIP_ + ":" + to_string(servPort_) + "\r\nContent-Type: "
                "application/x-www-form-urlencoded\r\nConnection: Keep-Alive\r\n\r\n";

    const char* p_in = p.c_str();
    ssize_t n = write(sockfd_, p_in, strlen(p_in));
    sleep(1);
    n = read(sockfd_, buff_, 4096);
    buff_[n] = 0;
    printf("\n%s", buff_);

    memset(buff_, 0, sizeof(buff_)/sizeof(char));
}


TreeHoleClient::~TreeHoleClient() { 
    if(sockfd_ != -1) {
        close(sockfd_);
        sockfd_ = -1;
        cout << "连接已关闭~" << endl;
    }
}


void TreeHoleClient::loop() {

    cout << "\n\n\t\t --- 这里是Sicrve的树洞, 你可以: --- " << endl;
    cout << "\t 1. 留言" << endl;
    cout << "\t 2. 获取最新留言" << endl;
    cout << "\t 请选择(q/Q 退出):";

    while(1) {
        string input;
        cin >> input;

        if(input.size() != 0) {
            if(input[0] == 'q' || input[0] == 'Q') {
                cout << endl << "欢迎下次再来~" << endl;
                break;
            } else if(input[0] == '1') {
                string msg;
                cout << endl << "请输入：";
                cin.ignore();
                getline(cin, msg);      // 允许有空格

                string p = "POST /msg HTTP/1.1\r\nHost: " + servIP_ + ":" + to_string(servPort_) + "\r\nContent-Type: "
                    "application/x-www-form-urlencoded\r\nConnection: Keep-Alive\r\n\r\n^" + username_ + "@" + msg + "^";

                const char* p_in = p.c_str();
                ssize_t n = write(sockfd_, p_in, strlen(p_in));
                sleep(1);
                n = read(sockfd_, buff_, 4096);
                if(n <= 0) {
                    cout << "服务器已中断！留言失败，请稍后再试！" << endl;
                    break;
                }

                buff_[n] = 0;
                printf("\n%s\n", buff_);
                memset(buff_, 0, sizeof(buff_)/sizeof(char));
                
                cout << "\n\t\t --- 这里是Sicrve的树洞, 你可以: --- " << endl;
                cout << "\t 1. 留言" << endl;
                cout << "\t 2. 获取最新留言" << endl;
                cout << "\t 请选择(q/Q 退出):";
                continue;

            } else if(input[0] == '2') {
                int cnt = 1;
                unsigned long key = 0;

                string year, month, day;
                cout << endl << "请输入获取留言的数量：(最大20条)";
                cin >> cnt;
                cnt = max(min(20, cnt), 0);
                
                cout << endl << "是否指定起始日期[y]/[n]：";
                char isdate;
                cin >> isdate;
                if(isdate == 'y' || isdate == 'Y') {
                    cout << endl << "请分别输入年月日（使用空格或换行间隔）：";
                    cin >> year >> month >> day;
                    
                    int nyear = stoi(year);
                    int nmonth = stoi(month);
                    int nday = stoi(day);
                    nyear = max(nyear, 1970);
                    nmonth = min(max(nmonth, 1), 12);
                    nday = min(max(nday, 1), 31);

                    struct tm ntime;
                    ntime.tm_year = nyear - 1900;
                    ntime.tm_mon = nmonth - 1;
                    ntime.tm_mday = nday;

                    key = static_cast<unsigned long>(mktime(&ntime));
                }

                string p = "GET /^" + to_string(key) + "@" + to_string(cnt) + "^ HTTP/1.1\r\nHost: " + servIP_ + ":" + to_string(servPort_) + "\r\nContent-Type: "
                    "application/x-www-form-urlencoded\r\nConnection: Keep-Alive\r\n\r\n";

                const char* p_in = p.c_str();
                ssize_t n = write(sockfd_, p_in, strlen(p_in));
                sleep(1);
                n = read(sockfd_, buff_, 4096);
                if(n == 0) {
                    cout << "获取失败！请稍后再试！" << endl;
                    break;
                } else {
                    buff_[n] = 0;
                    printf("%s", buff_);
                    memset(buff_, 0, sizeof(buff_)/sizeof(char));
                }

                cout << "\n\t\t --- 这里是Sicrve的树洞, 你可以: --- " << endl;
                cout << "\t 1. 留言" << endl;
                cout << "\t 2. 获取最新留言" << endl;
                cout << "\t 请选择(q/Q 退出):";
                continue;
            }
        } else {
            cout << endl << "输入错误，请重新输入：";
            continue;
        }

    }
}