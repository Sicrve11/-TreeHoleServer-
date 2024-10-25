/* @Author shigw    @Email sicrve@gmail.com */

#include "EventLoop.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include "base/Logger.h"
using namespace std;

__thread EventLoop* t_loopInThisThread = 0;

int createEventfd() {
    // eventfd是专门用于事件通知的，不可用于内容传递
    // 可以用于进程之间，也可以用于用户和内核态之间
    // eventfd内部有计数器，read会清零，write会增加计数器，如果大于0则说明有读任务
    int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if(evtfd < 0) {
        // LOG << "Failed in eventfd";
        abort();
    }
    return evtfd;
}

EventLoop::EventLoop() :
    poller_(new Epoll()), 
    wakeupFd_(createEventfd()),
    pwakeupChannel_(new Channel(this, wakeupFd_)),
    threadId_(CurrentThread::tid()),
    num_socket_(0),
    is_looping_(false), 
    is_quit_(false), 
    is_eventHandling_(false), 
    is_callingPendingFunctions_(false) {
    if(t_loopInThisThread) {
        // LOG << "Another EventLoop exists in this thread ";
        // LOG << "Another EventLoop " << t_loopInThisThread << " exists in this thread " << threadId_;
    } else {
        t_loopInThisThread = this;
    }

    // 设置该loop套接字的状态信息
    pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
    pwakeupChannel_->setReadHandler(bind(&EventLoop::handleRead, this));    // 非静态成员函数的第一个参数是this指针
    pwakeupChannel_->setConnHandler(bind(&EventLoop::handleConn, this));    // bind后的函数可以直接传递给function，通过function可以便捷调用
    
    // 将该loop持有的套接加入poller，用于在poller空转时通信，此时新加入的事件会放入等待队列中
    poller_->epoll_add(pwakeupChannel_, 0);
}

EventLoop::~EventLoop() {
    close(wakeupFd_);
    t_loopInThisThread = NULL;
}


// loop 接口
void EventLoop::wakeup() {      // 子reactor唤醒函数接口
    uint64_t one = 1;
    ssize_t n = writen(wakeupFd_, (char*)(&one), sizeof one);
    if (n != sizeof one) {
        // LOG << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
    }
}

void EventLoop::queueInLoop(Function&& func) {
    // 加线程锁，将某个事件加入子reactor等待执行队列
    {
        MutexLockGuard lock(mutex_);
        pendingFunctions_.emplace_back(std::move(func));
    }

    // 如果是外部进程调用，则说明是任务分配, 如果没有其他任务则进行唤醒;
    // 或者此时如果可执行并且有等待任务，就进行唤醒；
    if(!isInLoopThread() || is_callingPendingFunctions_) wakeup();
}

bool EventLoop::isInLoopThread() {
    return threadId_ == CurrentThread::tid();
}

void EventLoop::runInLoop(Function&& func) {
    if(isInLoopThread()) {
        func();
    } else {
        queueInLoop(std::move(func));
    }
}

void EventLoop::assertInLoopThread() { 
    assert(isInLoopThread()); 
}


// poller的接口
void EventLoop::addToPoller(SP_Channel channel, int timeout) {
    poller_->epoll_add(channel, timeout);
    ++num_socket_;
}

void EventLoop::updatePoller(SP_Channel channel, int timeout) {
    poller_->epoll_mod(channel, timeout);
}

void EventLoop::removeFromPoller(SP_Channel channel) {
    // shutDownWR(channel->getFd());
    poller_->epoll_del(channel);
    --num_socket_;
}


// 事件处理函数
void EventLoop::handleConn() {
    updatePoller(pwakeupChannel_, 0);
}              // 连接回调函数

void EventLoop::handleRead() {
    uint64_t one = 1;
    ssize_t n = readn(wakeupFd_, &one, sizeof one);
    if (n != sizeof one) {
        // LOG << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
    }
    // pwakeupChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
    pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
}             // 读回调函数

void EventLoop::doPendingFunctions() {
    std::vector<Function> functions;
    is_callingPendingFunctions_ = true;

    {
        MutexLockGuard lock(mutex_);
        functions.swap(pendingFunctions_);      // 拿到等待队列的函数
    }

    for(size_t i = 0; i < functions.size(); ++i) {
        functions[i]();
    }
    
    is_callingPendingFunctions_ = false;
}     // 执行等待的函数



// 核心函数
void EventLoop::loop() {
    // 开始事件循环，调用该函数必须是该eventloop
    assert(!is_looping_);
    assert(isInLoopThread());
    is_looping_ = true;
    is_quit_ = false;

    // LOG_TRACE << "EventLoop " << this << " start looping";
    std::vector<SP_Channel> ret;
    while(!is_quit_) {
        ret.clear();
        ret = poller_->poll();      // 新的线程在这里会被阻塞

        if(ret.size() > 0) {        // 真的有事件时才处理事件，否则处理等待事件或者查看是否有过期连接
            is_eventHandling_ = true;
            for(auto& it : ret) {
                it->handleEvents();     // 根据获取的事件来处理对应的事件
            }
            is_eventHandling_ = false;
        }

        doPendingFunctions();
        poller_->handleExpired();
    }
    is_looping_ = false;
}


// 半关闭连接
void EventLoop::shutDown(SP_Channel channel) {
    shutDownWR(channel->getFd());
}

void EventLoop::quit() {
    is_quit_ = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}