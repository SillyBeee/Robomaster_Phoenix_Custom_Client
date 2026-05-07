#pragma once

#include <iostream>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

// 禁用拷贝和移动
#define DISALLOW_COPY_AND_ASSIGN(className)           \
    className(const className &) = delete;            \
    className &operator=(const className &) = delete; \
    className(className &&) = delete;                 \
    className &operator=(className &&) = delete;

// 注册单例类（使用静态对象，自动释放）
#define REGISTER_SINGLETON(className)                   \
public:                                                 \
    /* 获取单例实例（返回引用，而非指针） */            \
    static className &GetInstance()                     \
    {                                                   \
        static className instance;                      \
        return instance;                                \
    }                                                   \
                                                        \
                                                        \
private:                                                \
    DISALLOW_COPY_AND_ASSIGN(className)
