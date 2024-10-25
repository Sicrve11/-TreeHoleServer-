/* @Author shigw    @Email sicrve@gmail.com */

#include <cstdlib>
#include <iostream>
#include <string>
#include <signal.h>
#include "EventLoop.h"
#include "Server.h"
#include "base/Logger.h"

using namespace std;

// 参数
int maxItems = 10;
std::string logPath = "./WebServer.log";
std::string FILE_PATH = "dumpFile.txt";
Server* myServer = nullptr;

int port = 8080;
int threadNum = 4;
int maxLevels = 10;
int maxCacheSize = 30;

void signalHandler(int signal);     // 信号处理函数，用于ctrl+c关闭服务器时调用

int main(int argc, char* argv[]) {

    if(argc != 5) {     // 检查是否正确调用
        printf("Usage : %s <Port> <threadNum> <skiplistLevel>\n", argv[0]);
        exit(1);
    }

    // 绑定信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    

    port = atoi(argv[1]);
    threadNum = atoi(argv[2]);
    maxLevels = atoi(argv[3]);
    maxCacheSize = atoi(argv[4]);
    // parse args 扩展执行程序时输入的参数
    // 待补充

    Logger::setLogFileName(logPath);    // 开启日志
    cout << "Logger started!" << endl;

    EventLoop mainLoop;      // 主reactor
    myServer = new Server(&mainLoop, threadNum, port, FILE_PATH, maxItems, maxLevels, maxCacheSize);    // 服务器

    // 启动
    myServer->start();
    cout << "Server started!" << endl;

    mainLoop.loop();

    return 0;
}

void signalHandler(int signal) {
    std::cout << "Signal (" << signal << ") received. Shutting down..." << std::endl;
    LOG << "Signal (" << signal << ") received. Shutting down...";
    delete myServer;  // 调用析构函数, 写回数据
    exit(signal);
}