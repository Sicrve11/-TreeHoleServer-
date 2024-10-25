/* @Author shigw    @Email sicrve@gmail.com */

#pragma once
#include <cstddef>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include "base/MutexLock.h"
#include "TreeHole.h"
using namespace std;

// Class template for Skip list
class SkipList {
    public: 
        SkipList(const string&, int=10, int=10);
        ~SkipList();

        // 增删查
        shared_ptr<TreeHole> createNode(const string&, const string&, int);
        shared_ptr<TreeHole> createNode(const string&, const string&, const string&, unsigned long, int);
        bool insertNode(const string&, const string&);
        bool insertNode(const string&, const string&, const string&, unsigned long);
        bool searchNode(const unsigned long);
        void deleteNode(unsigned long);

        int getRandomLevel();
        int getSize();

        // 获取留言
        // 按指定时间获取
        string getItems(unsigned long);              
        string getItems(unsigned long, int num);
        
        // 按指定数量获取
        string getItems(int num);


    private:
        int maxLevel_;              // 最大跳表层数 
        int skipListLevel_;         // 跳表层数 
        int elementCount_;          // 元素个数
        shared_ptr<TreeHole> header_;      // 头结点指针         

        bool insert(shared_ptr<TreeHole>&,  int);

        MutexLock mtx_;

        int maxItemsLimited_;       // 可获得的最大留言数
        string filename_;           // 本地存储文件
};

