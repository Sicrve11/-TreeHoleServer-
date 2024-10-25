/* @Author shigw    @Email sicrve@gmail.com */

#include "Epoll.h"
#include "Channel.h"
#include <cassert>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "base/Logger.h"

using namespace std;
const int EVENTSNUM = 4096;
const int EPOLLWAIT_TIME = 10000;

Epoll::Epoll():epollFd_(epoll_create1(EPOLL_CLOEXEC)), events_(EVENTSNUM) {
    assert(epollFd_ > 0);
}

Epoll::~Epoll() {}


void Epoll::epoll_add(SP_Channel request, int timeout) {
    int fd = request->getFd();

    if(timeout > 0) {
        add_timer(request, timeout);
        fd2http_[fd] = request->getHolder();
    }

    struct epoll_event event;
    event.data.fd = fd;
    event.events = request->getEvents();
    request->EqualAndUpdateLastEvents();

    fd2chan_[fd] = request;
    if(epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        perror("epoll_add error");
        fd2chan_[fd].reset();
    }
}

void Epoll::epoll_mod(SP_Channel request, int timeout) {
    int fd = request->getFd();
    if(timeout > 0) {
        add_timer(request, timeout);
    }

    if(!request->EqualAndUpdateLastEvents()) {  // 事件发生了变化时，才会更新epoll中注册的状态
        struct epoll_event event;
        event.data.fd = fd;
        event.events = request->getEvents();
        if(epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
            perror("epoll_mod error");
            fd2chan_[fd].reset();
        }
    }

}

void Epoll::epoll_del(SP_Channel request) {
    int fd = request->getFd();

    struct epoll_event event;
    event.data.fd = fd;
    event.events = request->getLastEvents();

    if(epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &event) < 0) {
        perror("epoll_del error");
    }
    fd2chan_[fd].reset();
    fd2http_[fd].reset();
}


std::vector<SP_Channel> Epoll::poll() {
    // while(true) {
    int event_count = epoll_wait(epollFd_, &*events_.begin(), events_.size(), EPOLLWAIT_TIME);
    if(event_count < 0) perror("epoll wait error");
    
    // 从内核态中拿到触发的事件
    vector<SP_Channel> poll_list;
    for(int i = 0; i < event_count; ++i) {
        int fd = events_[i].data.fd;
        SP_Channel cur_req = fd2chan_[fd];

        if(cur_req != nullptr) {
            cur_req->setRevents(events_[i].events);     // 拿到发生的事件
            cur_req->setEvents(0);                      // event是监控的事件集合
            poll_list.push_back(cur_req);
        } else {
            // LOG << "SP_cur_req is invalid";
        }
    }

    return poll_list;
    // if(poll_list.size() > 0) return poll_list;      // 如果没有事件则持续循环
    // }
}


void Epoll::add_timer(SP_Channel request_data, int timeout) {
    shared_ptr<HttpData> t = request_data->getHolder();
    if (t) {
        timerManager_.addTimer(t, timeout);
    } else {
        LOG << "timer add fail";
    }
}


void Epoll::handleExpired() {
    timerManager_.handleExpiredEvent();
}

