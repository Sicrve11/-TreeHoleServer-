/* @Author shigw    @Email sicrve@gmail.com */

#pragma once
#include <memory>
#include <vector>
#include "EventLoopThread.h"
#include "base/noncopyable.h"
#include "base/logger.h"

class EventLoopThreadPool : noncopyable {
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads);
    ~EventLoopThreadPool();

    void start();
    EventLoop* getNextLoop();
    EventLoop* getLeastLoop();

private:
    EventLoop* baseLoop_;   
    bool is_started;    
    int numThreads_;        // 总线程数 
    int nextThread_;        // 下一个分配的线程数

    // 线程池和对应的EventLoop事件
    vector<shared_ptr<EventLoopThread>> threads_;
    vector<EventLoop*> loops_;
};



