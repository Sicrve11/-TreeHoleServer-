/* @Author shigw    @Email sicrve@gmail.com */

// Channel类，表示每一个客户端连接的通道
// EPOLLIN ：表示对应的文件描述符可以读（包括对端SOCKET正常关闭）；
// EPOLLPRI：表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
// EPOLLOUT：表示对应的文件描述符可以写；
// EPOLLERR：表示对应的文件描述符发生错误；
// EPOLLHUP：表示对应的文件描述符被挂断；
// EPOLLET： 将EPOLL设为边缘触发(Edge Triggered)模式，这是相对于水平触发(Level Triggered)来说的。
// EPOLLONESHOT：只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里

#include <unistd.h>
#include <sys/epoll.h>
#include "Channel.h"
#include "EventLoop.h"
using namespace std;

Channel::Channel(EventLoop *loop) : loop_(loop), fd_(0), events_(0), lastEvents_(0)  {}
Channel::Channel(EventLoop *loop, int fd) : loop_(loop), fd_(fd), events_(0), lastEvents_(0) {}
Channel::~Channel() {
    // loop_->poller_->epoll_del(fd, events_);
    // close(fd_);
}

void Channel::handleRead() {
    if(readHandler_) readHandler_();
}

void Channel::handleWrite() {
    if(writeHandler_) writeHandler_();
}

void Channel::handleError(int fd, int err_num, std::string short_msg) {

}

void Channel::handleConn() {
    if(connHandler_) connHandler_();
}

// 按照类型进行事件处理
void  Channel::handleEvents() {
    events_ = 0;
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {       // 挂起没有读任务就直接返回
        events_ = 0;
        return;
    } else if (revents_ & EPOLLERR) {
        if (errorHandler_) errorHandler_();
        events_ = 0;
        return;
    } else if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        handleRead();
    } else if (revents_ & EPOLLOUT) {
        handleWrite();
    }
    
    handleConn();       // 连接后处理
}
