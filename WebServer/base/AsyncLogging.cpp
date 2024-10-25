/* @Author shigw    @Email sicrve@gmail.com */
// 异步将log写入文件的类，通过一个异步线程操作logfile对象，将logstream中的数据写入log
#include "AsyncLogging.h"
#include "Condition.h"
#include "MutexLock.h"
#include <cassert>
#include <cstddef>


AsyncLogging::AsyncLogging(const std::string logFileName_, int flushInterval)
  : basename_(logFileName_),
    thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
    mutex_(),
    cond_(mutex_),
    latch_(1),
    running_(false),
    flushInterval_(flushInterval), 
    outputBuffers_(),
    curBufferPtr_(new Buffer),
    nextBufferPtr_(new Buffer)
{
    assert(logFileName_.size() > 1);
    curBufferPtr_->initBuffer();
    nextBufferPtr_->initBuffer();
    outputBuffers_.reserve(16);
}

AsyncLogging::~AsyncLogging() {
    if(running_) stop();
}

// 管理写回线程
void AsyncLogging::start() {
    running_ = true;
    thread_.start();        // 开启线程，并执行写回函数
    latch_.wait();
}

void AsyncLogging::stop() {
    running_ = false;
    cond_.notify();
    thread_.join();
}

void AsyncLogging::append(const char* logline, int len) {
    MutexLockGuard lock(mutex_);
    if(curBufferPtr_->getAvailLen() > len) {     // 当前buffer够就直接写
        curBufferPtr_->append(logline, len);
    } else {
        outputBuffers_.push_back(curBufferPtr_);
        curBufferPtr_.reset();      // 相当于指针置空，减去原对象的指针引用次数，如果引用次数为0，则释放指向的内存
        
        if(nextBufferPtr_) {        // 如果下一个buffer存有数据，则转移对象
            curBufferPtr_ = std::move(nextBufferPtr_);      
        } else {                    // 否则就再申请一个
            curBufferPtr_.reset(new Buffer());
        }
        
        curBufferPtr_->append(logline, len);
        cond_.notify();
    }
}

void AsyncLogging::threadFunc() {
    assert(running_ == true);
    latch_.countDown();
    LogFile logfile(basename_);   // LogFile 是为了输出log而指定的  

    // 双buffer
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->initBuffer();
    newBuffer2->initBuffer();

    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while(running_) {
        assert(newBuffer1 && newBuffer1->getDataLen() == 0);
        assert(newBuffer2 && newBuffer2->getDataLen() == 0);
        assert(buffersToWrite.empty());
        
        // 交换缓冲队列时涉及到写入缓冲线程之间的锁
        {
            MutexLockGuard lock(mutex_);
            if(outputBuffers_.empty()) {    // 如果此时outputBuffers_是空的，就停止等待，再次写入会被唤醒
                cond_.waitForSeconds(flushInterval_);
            }

            // 无论何种方式，都是写入了curBufferPtr_，因此首先将curBufferPtr_加入输出队列，然后换上新的buffer
            outputBuffers_.push_back(curBufferPtr_);
            curBufferPtr_.reset();              
            curBufferPtr_ = std::move(newBuffer1);

            buffersToWrite.swap(outputBuffers_);    // 将输出的buffer和

            if(!nextBufferPtr_) {       // nextBufferPtr_会有两种状态，一种是空的buffer，另一种是空指针
                nextBufferPtr_ = std::move(newBuffer2);
            }
        }

        // 下面这一段是将buffersToWrite写回文件
        assert(!buffersToWrite.empty());
        if (buffersToWrite.size() > 25) {       // 当超过25个缓冲块时进行裁剪
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
        }

        for(size_t i = 0; i < buffersToWrite.size(); ++i) {
            logfile.append(buffersToWrite[i]->getStartPtr(), buffersToWrite[i]->getDataLen());
        }

        // 只保留两个buffer用于复用
        if (buffersToWrite.size() > 2) {      
            // drop non-bzero-ed buffers, avoid trashing
            buffersToWrite.resize(2);
        }

        if (!newBuffer1) {
            assert(!buffersToWrite.empty());
            newBuffer1 = buffersToWrite.back();
            buffersToWrite.pop_back();
            newBuffer1->clearBuffer();
        }

        if (!newBuffer2) {
            assert(!buffersToWrite.empty());
            newBuffer2 = buffersToWrite.back();
            buffersToWrite.pop_back();
            newBuffer2->clearBuffer();
        }

        buffersToWrite.clear();
        logfile.flush();        // 直接写入文件
    }
    logfile.flush();
}
