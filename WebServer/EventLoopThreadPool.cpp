/* @Author shigw    @Email sicrve@gmail.com */

#include "EventLoopThreadPool.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
   :baseLoop_(baseLoop), 
    is_started(false), 
    numThreads_(numThreads), 
    nextThread_(0) {
    if (numThreads <= 0) {
        LOG << "numThreads_ <= 0";
        abort();
    }
}

EventLoopThreadPool::~EventLoopThreadPool() {
    LOG << "~EventLoopThreadPool()";
}

void EventLoopThreadPool::start() {
    baseLoop_->assertInLoopThread();
    is_started = true;
    for(int i = 0; i < numThreads_; ++i) {
        shared_ptr<EventLoopThread> t(new EventLoopThread());
        threads_.push_back(t);
        loops_.push_back(t->startLoop()); // 主线程会创建子线程、并确保子线程正常运行，返回指向子线程持有的loop（该loop是局部变量）
    }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
    baseLoop_->assertInLoopThread();
    assert(is_started);
    EventLoop* loop;
    if(!loops_.empty()) {   // 简单的负载均衡
        loop = loops_[nextThread_];
        nextThread_ = (nextThread_ + 1) % numThreads_;
    }
    return loop;
}

EventLoop* EventLoopThreadPool::getLeastLoop() {
    baseLoop_->assertInLoopThread();
    assert(is_started);
    EventLoop* loop;
    int idx = 0;
    int cnt_socket = loops_[idx]->get_socket_num();
    
    for(int i = 0; i < numThreads_; i++) {
        int tmp = loops_[i]->get_socket_num();
        if(tmp < cnt_socket) {
            idx = i;
            cnt_socket = tmp;
        }
    }
    loop = loops_[idx];
    return loop;
}