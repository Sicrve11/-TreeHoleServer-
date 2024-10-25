/* @Author shigw    @Email sicrve@gmail.com */

#include "SkipList.h"
#include "TreeHole.h"
#include <sstream>
#include <cstddef>
#include <string>
#include "base/Logger.h"

// construct skip list
SkipList::SkipList(const string& fileName, int max_item, int max_level)
  : maxLevel_(max_level), 
    skipListLevel_(0), 
    elementCount_(0), 
    maxItemsLimited_(max_item), 
    filename_(fileName) {

    this->header_ = createNode("", "", maxLevel_);

    // 读取本地文件
    std::ifstream f_in(filename_, ios::in);
    if(f_in.is_open()) {
        while (!f_in.eof())
        {
            string line;
            getline(f_in, line);
            std::stringstream linedata(line);
            
            string username, whisper, dateTime;
            unsigned long key;
            size_t pos = line.find("|");
            for(int i = 0; i < 4; i++) {
                line = line.substr(pos+1);
                pos = line.find("|");
                if(i == 0) {
                    username = line.substr(0, pos);
                } else if(i == 1) {
                    whisper = line.substr(0, pos);
                } else if(i == 2) {
                    dateTime = line.substr(0, pos);
                } else if(i == 3) {
                    string tmpkey = line.substr(0, pos);
                    key = std::atoll(tmpkey.c_str());
                }
            }
            
            this->insertNode(username, whisper, dateTime, key);
        }
    }
    f_in.close();
}

SkipList::~SkipList() {
    // 覆盖写入文件
    std::ofstream f_out(filename_);
    if (!f_out.is_open()) {
        LOG << "写入文件失败!";
    } else {
        shared_ptr<TreeHole> current = header_;
        current = current->forward[0];

        for (int i = 0; i < elementCount_; i++) {
            f_out << current->getSaveItem() << endl;
            current = current->forward[0];
        }
        f_out.close();
    }
}

// 获取随机高度
int SkipList::getRandomLevel(){
    int k = 1;
    while (rand() % 2) k++;
    k = (k < maxLevel_) ? k : maxLevel_;
    return k;
}

// 使用 make_shared() 创建节点
shared_ptr<TreeHole> SkipList::createNode(const string& username, const string& whisper, int level) {
    shared_ptr<TreeHole> pNode = make_shared<TreeHole>(username, whisper, level);
    return pNode;
}

shared_ptr<TreeHole> SkipList::createNode(const string& username, const string& whisper, const string& dateTime, unsigned long key, int level) {
    shared_ptr<TreeHole> pNode = make_shared<TreeHole>(username, whisper, dateTime, key, level);
    return pNode;
}

bool SkipList::searchNode(const unsigned long key) {
    shared_ptr<TreeHole> current = header_;

    // 从高到低的链表进行查询
    for (int i = skipListLevel_; i >= 0; i--) {
        while (current->forward[i] && current->forward[i]->getKey() < key) {
            current = current->forward[i];
        }
    }

    current = current->forward[0];      // 最终使用的是初级链表进行判断

    if (current and current->getKey() == key) {
        return true;
    }

    return false;
}

bool SkipList::insertNode(const string& username, const string& whisper) {
    int random_level = getRandomLevel();    // 为了保证随机访问，随机生成插入的最高层
    shared_ptr<TreeHole> inserted_node = createNode(username, whisper, random_level);
    return insert(inserted_node, random_level);
}


bool SkipList::insertNode(const string& username, const string& whisper, const string& dateTime, unsigned long key) {
    int random_level = getRandomLevel();    // 为了保证随机访问，随机生成插入的层
    shared_ptr<TreeHole> inserted_node = createNode(username, whisper, dateTime, key, random_level);
    return insert(inserted_node, random_level);
}

bool SkipList::insert(shared_ptr<TreeHole>& inserted_node, int random_level) {
    size_t key = inserted_node->getKey();

    // mtx_.lock(); 
    MutexLockGuard lock(mtx_);
    shared_ptr<TreeHole> current = this->header_;

    // 首先查找目标节点，并记录每一层跳表的修改点
    vector<shared_ptr<TreeHole>> update(maxLevel_ + 1, nullptr); 
    for(int i = skipListLevel_; i >= 0; i--) {
        while(current->forward[i] != NULL && current->forward[i]->getKey() < key) {
            current = current->forward[i]; 
        }
        update[i] = current;
    }

    // 判断是否找到，如果已存在就直接返回
    current = current->forward[0];
    if (current != NULL && current->getKey() == key) {
        inserted_node = nullptr;
        return false;
    }

    // 插入数据
    if (current == NULL || current->getKey() != key) {
        
        if (random_level > skipListLevel_) {
            for (int i = skipListLevel_+1; i < random_level+1; i++) {
                update[i] = header_;
            }
            skipListLevel_ = random_level;
        }

        // 插入
        for (int i = 0; i <= random_level; i++) {
            inserted_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = inserted_node;
        }
        elementCount_ ++;
    }

    return true;
}


void SkipList::deleteNode(unsigned long key) {
    MutexLockGuard lock(mtx_);

    shared_ptr<TreeHole> current = this->header_;
    vector<shared_ptr<TreeHole>> update(maxLevel_ + 1, nullptr);
    for (int i = skipListLevel_; i >= 0; i--) {
        while (current->forward[i] !=NULL && current->forward[i]->getKey() < key) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    current = current->forward[0];
    if (current != NULL && current->getKey() == key) {
        for (int i = 0; i <= skipListLevel_; i++) {     // 逐级更新
            if (update[i]->forward[i] != current) 
                break;
            update[i]->forward[i] = current->forward[i];
        }

        while (skipListLevel_ > 0 && header_->forward[skipListLevel_] == 0) {
            skipListLevel_ --; 
        }

        elementCount_ --;
    }

    return;
}

// Get current SkipList size 
int SkipList::getSize() { 
    return elementCount_;
}

// 按指定时间获取
string SkipList::getItems(unsigned long key) {
    int cnt = min(maxItemsLimited_, elementCount_);
    return getItems(key, cnt);
}       


string SkipList::getItems(unsigned long key, int num) {
    int cnt = min(num, min(maxItemsLimited_, elementCount_));

    shared_ptr<TreeHole> current = header_;

    // 从高到低的链表进行查询
    for (int i = skipListLevel_; i >= 0; i--) {
        while (current->forward[i] && current->forward[i]->getKey() < key) {
            current = current->forward[i];
        }
    }

    current = current->forward[0];      // 最终使用的是初级链表进行判断

    string items = "";
    if (current && current->getKey() >= key) {

        while(current && cnt > 0) {
            items += current->getItem();
            current = current->forward[0];
            cnt--;
        }

    } else {
        items = "no records!";
    }

    return items;
}

// 按指定数量获取
string SkipList::getItems(int num) {
    int cnt = min(num, min(maxItemsLimited_, elementCount_));

    if(cnt == 0) return "no records!";

    int passnum = elementCount_ - cnt;
    shared_ptr<TreeHole> current = header_;

    while(passnum >= 0) {
        current = current->forward[0];
        passnum--;
    }

    string items = "";
    while(current && cnt > 0) {
        items += current->getItem();
        current = current->forward[0];
        cnt--;
    }

    return items;
}

