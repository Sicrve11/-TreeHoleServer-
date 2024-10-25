/* @Author shigw    @Email sicrve@gmail.com */

#pragma once
#include <cstddef>
#include <unordered_map>
#include <vector>
#include "base/noncopyable.h"
#include "base/MutexLock.h"
#include <iostream>
using namespace std;

template<typename key_t, typename value_t>
class LRUCache : public noncopyable {
public:
    typedef typename std::pair<key_t, value_t> key_value_pair_t;

    LRUCache(size_t max_size) : max_size_(max_size), start_idx_(0), end_idx_(0) {
        cache_items_.resize(max_size_);
        cache_keys_.resize(max_size_);
        cache_list_.resize(max_size_);

        // 初始化静态双向链表
        for(int i = 0; i < max_size_; i++) {
            cache_list_[i].resize(3);
            cache_list_[i][0] = -1;
            cache_list_[i][1] = i;
            cache_list_[i][2] = -1;
        }
    }

    ~LRUCache() { }

    void put(const key_t& key, const value_t& value) {

		auto it = cache_items_map_.find(key);
		if (it != cache_items_map_.end()) {     // 如果存在, 就将其挪到链表最前

            int idx = cache_items_map_[key];

            if(idx != start_idx_) {
                MutexLockGuard lock(mtx_);

                // 处理后半段
                if(idx == end_idx_) {
                    int pre = cache_list_[idx][0];
                    cache_list_[pre][2] = cache_list_[idx][2];
                    end_idx_ = pre;
                } else {
                    int pre = cache_list_[idx][0];
                    int post = cache_list_[idx][2];

                    cache_list_[pre][2] = post;
                    cache_list_[post][0] = pre;
                }

                // 处理前半段
                cache_list_[start_idx_][0] = idx;
                cache_list_[idx][0] = -1;
                cache_list_[idx][2] = start_idx_;
                start_idx_ = idx;
            }

		} else {        // 如果不存在, 就将其挪到链表最前，并处理删除问题                                         
            int tmpsize = cache_items_map_.size();
            
            if(tmpsize == 0) {
                start_idx_ = tmpsize;
                end_idx_ = tmpsize; 
            } else if(tmpsize < max_size_) {
                MutexLockGuard lock(mtx_);

                cache_list_[tmpsize][0] = -1;
                cache_list_[tmpsize][2] = start_idx_;
                cache_list_[start_idx_][0] = tmpsize;
                start_idx_ = tmpsize;
            } else {       // 需要删除
                MutexLockGuard lock(mtx_);
    
                tmpsize = end_idx_;

                // 删除键值下标，并调整双向链表中的链接情况
                string key_end = cache_keys_[tmpsize];
                cache_items_map_.erase(key_end);
                int pre = cache_list_[tmpsize][0];
                cache_list_[pre][2] = -1;
                end_idx_ = pre;

                // 然后将这个节点插入到链表头节点处
                cache_list_[tmpsize][0] = -1;
                cache_list_[tmpsize][2] = start_idx_;
                cache_list_[start_idx_][0] = tmpsize;
                start_idx_ = tmpsize;
            }
            
            // 插入新的值
            cache_keys_[tmpsize] = key;
            cache_items_[tmpsize] = value;
            cache_items_map_[key] = tmpsize;
        }
    }

    bool get(const key_t& key, value_t& value_ans) {
        auto it = cache_items_map_.find(key);
		if (it == cache_items_map_.end()) {
			return false;
		} else {
			int idx = cache_items_map_[key];
            value_ans = cache_items_[idx];
            // put(key, value_ans);
            return true;
		}
    }
    
    bool exists(const key_t& key) const {
        return cache_items_map_.find(key) == cache_items_map_.end();
    }
    
    size_t size() const {
        return cache_items_map_.size();
    }

private:
    size_t max_size_;
    size_t start_idx_;
    size_t end_idx_;
    
    MutexLock mtx_;
    
    vector<value_t> cache_items_;
    vector<value_t> cache_keys_;
    vector<vector<int>> cache_list_;        // 通过数组的方式构建静态双向链表
    unordered_map<key_t, int> cache_items_map_;
};