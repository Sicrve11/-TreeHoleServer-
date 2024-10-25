/* @Author shigw    @Email sicrve@gmail.com */

#include "HttpData.h"
#include <cstddef>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <iostream>
#include "Channel.h"
#include "EventLoop.h"
#include "LRUCache.h"
#include "Util.h"
#include "base/Logger.h"
#include "time.h"

using namespace std;

const __uint32_t DEFAULT_EVENT = EPOLLIN | EPOLLET | EPOLLONESHOT;
const int DEFAULT_EXPIRED_TIME = 2000;              // 2000 ms
const int DEFAULT_KEEP_ALIVE_TIME = 5 * 60 * 1000;  // 30000 ms

HttpData::HttpData(EventLoop *loop, int connfd, std::shared_ptr<SkipList> skth, std::shared_ptr<LRUCache<string, string>> lru)
    : loop_(loop),
      channel_(new Channel(loop, connfd)),
      fd_(connfd),
      error_(false),
      skTreeHole_(skth),
      lru_cache_(lru),
      connectionState_(H_CONNECTED),
      method_(METHOD_GET),
      HTTPVersion_(HTTP_11),
      nowReadPos_(0),
      state_(STATE_PARSE_URI),
      hState_(H_START),
      keepAlive_(false) {
    // 绑定事务函数
    channel_->setEvents(DEFAULT_EVENT);     
    channel_->setReadHandler(bind(&HttpData::handleRead, this));
    channel_->setWriteHandler(bind(&HttpData::handleWrite, this));
    channel_->setConnHandler(bind(&HttpData::handleConn, this));
}

HttpData::~HttpData() { 

    // 释放资源
    channel_.reset();
    skTreeHole_.reset();
    // LOG << "FD = " << fd_ << " connection closed!\n";
    // cout << "FD = " << fd_ << " connection closed!" << endl;
    close(fd_);     // 关闭连接 
}


// 完成一个请求后状态清零
void HttpData::reset() {
    inBuffer_.clear();
    fileName_.clear();
    path_.clear();
    nowReadPos_ = 0;
    state_ = STATE_PARSE_URI;
    hState_ = H_START;
    headers_.clear();

    if (timer_.lock()) {    // 脱离timer
        // 通过weak_ptr获取对应的shared_ptr, 这样会使该对象引用加1
        shared_ptr<TimerNode> my_timer(timer_.lock());
        my_timer->clearReq();
        timer_.reset();
    }
}

void HttpData::seperateTimer() {
    
    // cout << "seperateTimer" << endl;
    if (timer_.lock()) {
        shared_ptr<TimerNode> my_timer(timer_.lock());
        my_timer->clearReq();
        timer_.reset();
    }
}


// 对请求行的解析
URIState HttpData::parseURI() {
    string& str = inBuffer_;

    // 01 首先分离请求行
    size_t pos = str.find("\r\n", nowReadPos_);
    if (pos < 0) return PARSE_URI_AGAIN;
    string request_line = str.substr(0, pos);
    if (str.size() > pos + 1) str = str.substr(pos + 1);
    else str.clear();

    // 02 解析Method
    int posGet = request_line.find("GET");
    int posPost = request_line.find("POST");
    int posHead = request_line.find("HEAD");

    if (posGet >= 0) { 
        pos = posGet;
        method_ = METHOD_GET;
    } else if (posPost >= 0) {
        pos = posPost;
        method_ = METHOD_POST;
    } else if (posHead >= 0) {
        pos = posHead;
        method_ = METHOD_HEAD;
    } else {
        return PARSE_URI_ERROR;
    }

    // 03 请求的filename or 内容
    pos = request_line.find("/", pos);
    if (pos < 0) {
        fileName_ = "index.html";
        HTTPVersion_ = HTTP_11;
        return PARSE_URI_SUCCESS;
    } else {
        size_t _pos = request_line.find(' ', pos);
        if (_pos < 0)
            return PARSE_URI_ERROR;
        else {
            if (_pos - pos > 1) {
                fileName_ = request_line.substr(pos + 1, _pos - pos - 1);

                if(method_ == METHOD_GET) {
                    size_t pos1 = request_line.find('^', pos);
                    if(pos1 != request_line.npos) {
                        size_t pos2 = request_line.find('^', pos1+1);
                        if(pos2 != request_line.npos) {
                            fileName_ = request_line.substr(pos1+1, pos2-pos1-1);
                        } else {
                            return PARSE_URI_ERROR;
                        }
                    }
                }
            } else fileName_ = "index.html";
        }
        pos = _pos;
    }

    // 04 HTTP 版本号
    pos = request_line.find("/", pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else {
        if (request_line.size() - pos <= 3) return PARSE_URI_ERROR;
        else {
            string ver = request_line.substr(pos+1, 3);
            if (ver == "1.0")
                HTTPVersion_ = HTTP_10;
            else if (ver == "1.1")
                HTTPVersion_ = HTTP_11;
            else
                return PARSE_URI_ERROR;
        }
    }

    return PARSE_URI_SUCCESS;
}

// 对头部状态解析
HeaderState HttpData::parseHeaders() {
    string &str = inBuffer_;
    int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
    int now_read_line_begin = 0;
    bool notFinish = true;
    size_t i = 0;
    for (; i < str.size() && notFinish; ++i) {
        switch (hState_) {
            case H_START: {
                if (str[i] == '\n' || str[i] == '\r') break;
                hState_ = H_KEY;
                key_start = i;
                now_read_line_begin = i;
                break;
            }
            case H_KEY: {
                if (str[i] == ':') {
                    key_end = i;
                    if (key_end - key_start <= 0) return PARSE_HEADER_ERROR;
                    hState_ = H_COLON;
                } else if (str[i] == '\n' || str[i] == '\r')
                    return PARSE_HEADER_ERROR;
                break;
            }
            case H_COLON: {
                if (str[i] == ' ') {
                    hState_ = H_SPACES_AFTER_COLON;
                } else
                    return PARSE_HEADER_ERROR;
                break;
            }
            case H_SPACES_AFTER_COLON: {
                hState_ = H_VALUE;
                value_start = i;
                break;
            }
            case H_VALUE: {
                if (str[i] == '\r') {
                    hState_ = H_CR;
                    value_end = i;
                    if (value_end - value_start <= 0) return PARSE_HEADER_ERROR;
                } else if (i - value_start > 255)
                    return PARSE_HEADER_ERROR;
                break;
            }
            case H_CR: {
                if (str[i] == '\n') {
                    hState_ = H_LF;
                    string key(str.begin() + key_start, str.begin() + key_end);
                    string value(str.begin() + value_start, str.begin() + value_end);
                    headers_[key] = value;
                    now_read_line_begin = i;
                } else
                    return PARSE_HEADER_ERROR;
                break;
            }
            case H_LF: {
                if (str[i] == '\r') {
                    hState_ = H_END_CR;
                } else {
                    key_start = i;
                    hState_ = H_KEY;
                }
                break;
            }
            case H_END_CR: {
                if (str[i] == '\n') {
                    hState_ = H_END_LF;
                } else
                    return PARSE_HEADER_ERROR;
                break;
            }
            case H_END_LF: {
                notFinish = false;
                key_start = i;
                now_read_line_begin = i;
                break;
            }
        }
    }
    if (hState_ == H_END_LF) {
        str = str.substr(i-1);
        return PARSE_HEADER_SUCCESS;
    }
    str = str.substr(now_read_line_begin);
    return PARSE_HEADER_AGAIN;
}

// 对请求体进行解析
AnalysisState HttpData::analysisRequest() {
    if (method_ == METHOD_POST) {
        // 接收数据，并插入跳表
        // ^name@whisper^
        int poststate = 0;
        string& str = inBuffer_;
        cout << str << endl;
        size_t pos1 = str.find('^');
        if(pos1 != str.npos) {
            str = str.substr(pos1+1);
            pos1 = str.find('^');
            if(pos1 != str.npos) {
                string treeHoleMsg = str.substr(0, pos1);
                str = str.substr(pos1+1);

                size_t pos2 = treeHoleMsg.find('@');
                if(pos2 != treeHoleMsg.npos) {
                    string thName = treeHoleMsg.substr(0, pos2);
                    string thWhisper = treeHoleMsg.substr(pos2+1);

                    skTreeHole_->insertNode(thName, thWhisper);     // 插入跳表
                    // LOG << "insert success! " << thName << "@" << thWhisper;
                } else {
                    poststate = 3;
                }
            } else {
                poststate = 2;
            }
        } else {
            poststate = 1;
        }

        // 编辑返回数据
        string header;
        string body;
        if(poststate == 0) {
            header += string("HTTP/1.1 200 OK\r\n");
            body = "留言成功! \n";
        } else {
            header += string("HTTP/1.1 400 Bad Request\r\n");
            if(poststate == 3) {
                body = "数据格式错误! \n";
            } else {
                body = "边界符丢失! \n";
            }
        }

        if(headers_.find("Connection") != headers_.end() && headers_["Connection"] == "Keep-Alive")
        {
            keepAlive_ = true;
            header += string("Connection: Keep-Alive\r\n") +  
            "timeout=" + to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
        }

        vector<char> data_encode(body.begin(), body.end());
        header += string("Content-length: ") + to_string(data_encode.size()) + "\r\n\r\n";
        outBuffer_ += header + string(data_encode.begin(), data_encode.end());
        return ANALYSIS_SUCCESS;

    } else if (method_ == METHOD_GET || method_ == METHOD_HEAD) {
        // 根据请求返回获取数据
        string header = "";
        string items = "";

        // 首先检查缓存
        if(lru_cache_->get(fileName_, header)) {
            outBuffer_ += header;

        } else {
            // 如果缓存不命中则才进行报文组装
            header += "HTTP/1.1 200 OK\r\n";
            if (headers_.find("Connection") != headers_.end() &&
                (headers_["Connection"] == "Keep-Alive" || headers_["Connection"] == "keep-alive")) {
                keepAlive_ = true;
                header += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout=" + 
                        to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
            }

            if(fileName_ == "index.html") {
                items += "<html><title>Sicrve's TreeHole</title>";
                items += "<body bgcolor=\"ffffff\">";
                items += "<hr><em> 欢迎来到 Sicrve's 树洞服务器！欢迎留言！ </em></body></html>";
            } else {
                size_t dot_pos = fileName_.find('@');
                unsigned long get_key = 0;
                int get_num = 0;
        
                if (dot_pos == fileName_.npos) {
                    header.clear();
                    handleError(fd_, 404, "Not Found!");
                    return ANALYSIS_ERROR;
                } else {
                    string tmp;
                    tmp = fileName_.substr(0, dot_pos);
                    get_key = stoi(tmp);
                    tmp = fileName_.substr(dot_pos+1);
                    get_num = stoi(tmp);
                }

                // 获取指定留言数据
                cout << "get_key: " << get_key << endl;
                cout << "get_num: " << get_num << endl;

                if(get_key != 0 && get_num != 0) {
                    items += skTreeHole_->getItems(get_key, get_num);
                } else if(get_key == 0) {
                    items += skTreeHole_->getItems(get_num);
                } else {
                    items += skTreeHole_->getItems(get_key);
                }
            }

            // cout << "items: " << items << endl;
            header += "Content-type: text/plain\r\n";
            header += "Content-Length: " + to_string(items.size()) + "\r\n";
            header += "Server: Sicrve's TreeHole Server\r\n";

            // 头部结束
            header += "\r\n";

            outBuffer_ += header;
            if (method_ == METHOD_HEAD) return ANALYSIS_SUCCESS;
            outBuffer_ += items;

            lru_cache_->put(fileName_, outBuffer_);
        }

        return ANALYSIS_SUCCESS;
    }

    return ANALYSIS_ERROR;
}


// 分别处理不同的事件
void HttpData::handleRead() {
    // __uint32_t &events_ = channel_->getEvents();
    do {
        // 00 读取数据 && 异常检测
        bool zero = false;
        int read_num = readn(fd_, inBuffer_, zero);
        // LOG << "Request: " << inBuffer_;
        if (connectionState_ == H_DISCONNECTING) {
            inBuffer_.clear();
            break;
        }
        // cout << inBuffer_ << endl;
        if (read_num < 0) {
            perror("1");
            error_ = true;
            handleError(fd_, 400, "Bad Request");
            break;
        } else if (zero) {
            // 有请求出现但是读不到数据，可能是Request
            // Aborted，或者来自网络的数据没有达到等原因
            // 最可能是对端已经关闭了，统一按照对端已经关闭处理
            // error_ = true;
            connectionState_ = H_DISCONNECTING;
            if (read_num == 0) {
                break;
            }
        }

        // 01 解析URL
        if (state_ == STATE_PARSE_URI) {
            URIState flag = this->parseURI();

            if (flag == PARSE_URI_AGAIN)
                break;
            else if (flag == PARSE_URI_ERROR) {
                perror("2");
                // LOG << "FD = " << fd_ << "******PARSE_URI_ERROR******";
                inBuffer_.clear();
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                break;
            } else
                state_ = STATE_PARSE_HEADERS;
        }

        // 02 解析头部
        if (state_ == STATE_PARSE_HEADERS) {
            HeaderState flag = this->parseHeaders();
            if (flag == PARSE_HEADER_AGAIN)
                break;
            else if (flag == PARSE_HEADER_ERROR) {
                perror("3");
                // LOG << "FD = " << fd_ << "******PARSE_HEADER_ERROR******";
                error_ = true;
                handleError(fd_, 400, "Bad Request");
                break;
            }

            state_ = STATE_ANALYSIS;
        }

        // 03 解析数据体
        if (state_ == STATE_ANALYSIS) {
            AnalysisState flag = this->analysisRequest();
            if (flag == ANALYSIS_SUCCESS) {
                state_ = STATE_FINISH;
                break;
            } else {
                // cout << "state_ == STATE_ANALYSIS" << endl;
                error_ = true;
                break;
            }
        }
    } while(false);

    if(!error_) {
        if (outBuffer_.size() > 0) {   // 如果有要发送的数据，就发送
            handleWrite();          
            outBuffer_.clear();
        }

        // error_ may change
        if (!error_ && state_ == STATE_FINISH) {
            this->reset();          // 状态清零

            if (inBuffer_.size() > 0) {
                if (connectionState_ != H_DISCONNECTING) handleRead();      // 对端没关闭
            }
        }
    }
}

void HttpData::handleWrite() {
    if (!error_ && connectionState_ != H_DISCONNECTED) {
        __uint32_t &events_ = channel_->getEvents();
        if (writen(fd_, outBuffer_) < 0) {
            perror("writen");
            events_ = 0;
            error_ = true;
        }
        if (outBuffer_.size() > 0) events_ |= EPOLLOUT;
    }
}


// 每次处理完后对连接状态的更新
void HttpData::handleConn() {
    seperateTimer();
    __uint32_t &events_ = channel_->getEvents();
    if (!error_ && connectionState_ == H_CONNECTED) {
        if (events_ != 0) {
            int timeout = DEFAULT_EXPIRED_TIME;
            if (keepAlive_) timeout = DEFAULT_KEEP_ALIVE_TIME;
            if ((events_ & EPOLLIN) && (events_ & EPOLLOUT)) {
                events_ = __uint32_t(0);
                events_ |= EPOLLOUT;
            }
            // events_ |= (EPOLLET | EPOLLONESHOT);
            events_ |= EPOLLET;
            loop_->updatePoller(channel_, timeout);

        } else if (keepAlive_) {
            events_ |= (EPOLLIN | EPOLLET);
            // events_ |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
            int timeout = DEFAULT_KEEP_ALIVE_TIME;
            loop_->updatePoller(channel_, timeout);
        } else {
            loop_->shutDown(channel_);
            loop_->runInLoop(bind(&HttpData::handleClose, shared_from_this()));
        }
    } else if (!error_ && connectionState_ == H_DISCONNECTING && (events_ & EPOLLOUT)) {
        events_ = (EPOLLOUT | EPOLLET);
    } else {        // 出错关闭连接
        loop_->runInLoop(bind(&HttpData::handleClose, shared_from_this()));
    }
}

void HttpData::handleError(int fd, int err_num, string short_msg) {
    short_msg = " " + short_msg;
    char send_buff[4096];
    string body_buff, header_buff;
    body_buff += "<html><title>哎~出错了</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(err_num) + short_msg;
    body_buff += "<hr><em> Sicrve's TreeHole Server</em>\n</body></html>";

    header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
    header_buff += "Content-Type: text/html\r\n";
    header_buff += "Connection: Close\r\n";
    header_buff += "Content-Length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "Server: Sicrve's TreeHole Server\r\n";
    ;
    header_buff += "\r\n";
    
    // 错误处理不考虑writen不完的情况
    sprintf(send_buff, "%s", header_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
    sprintf(send_buff, "%s", body_buff.c_str());
    writen(fd, send_buff, strlen(send_buff));
}

void HttpData::handleClose() {
    connectionState_ = H_DISCONNECTED;
    shared_ptr<HttpData> guard(shared_from_this());     // 防止直接进入析构
    loop_->removeFromPoller(channel_);
}

// 建立连接的回调函数，channel设置为边缘触发模式
void HttpData::newEvent() {
    loop_->addToPoller(channel_, DEFAULT_EXPIRED_TIME);
}
