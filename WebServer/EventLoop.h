/* @Author shigw    @Email sicrve@gmail.com */

#pragma once
#include <memory>
#include <functional>
#include <vector>
#include "Channel.h"
#include "Epoll.h"
#include "Util.h"
#include "base/CurrentThread.h"
#include "base/Logger.h"
#include "base/Thread.h"

using namespace std;

class EventLoop {
public:
    typedef function<void()> Function;
    EventLoop();
    ~EventLoop();

    // 核心函数
    void loop();
    void quit();

    // 外部接口函数
    void runInLoop(Function&& func);
    void queueInLoop(Function&& func);
    bool isInLoopThread();
    void assertInLoopThread();

    // poller的接口
    void addToPoller(SP_Channel channel, int timeout = 0);
    void updatePoller(SP_Channel channel, int timeout = 0);
    void removeFromPoller(SP_Channel channel);

    // 半关闭连接
    void shutDown(SP_Channel channel);

    // 获取连接数
    inline int get_socket_num() {
        return num_socket_;
    }

private:
    shared_ptr<Epoll> poller_;      // EventLoop自带的poller
    vector<Function> pendingFunctions_;     // 等待队列

    // eventloop中绑定的channel用于通信，异步唤醒
    int wakeupFd_;                     
    SP_Channel pwakeupChannel_;

    // one loop one thread
    const pid_t threadId_;  
    mutable MutexLock mutex_; 
    int num_socket_;            // 当前reactor中的连接数

    // reactor的各种状态
    bool is_looping_;                       // 是否进行事件循环
    bool is_quit_;                          // 是否停止事件循环
    bool is_eventHandling_;                 // 是否正在处理事件
    bool is_callingPendingFunctions_;       // 是否调用等待处理函数

    // reactor的内部函数, 用于处理主子reactor的任务分配，包括唤醒和任务执行
    void handleRead();              // 读回调函数
    void handleConn();              // 连接回调函数
    void wakeup();                  // 异步唤醒subreactor的epoll_wait
    void doPendingFunctions();      // 执行等待的函数，这些函数都是绑定了this指针的channel成员函数，因此可以直接
};  


