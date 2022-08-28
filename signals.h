#pragma once

#include "intrusive_list.h"

#include <functional>
#include <unordered_map>

namespace signals {

template <typename T>
struct signal;

template <typename... Args>
struct signal<void(Args...)>
{
    using callback_t = std::function<void(Args...)>;

    struct ConnectionTag;

    struct connection : intrusive::list_element<ConnectionTag>
    {
        connection() = default;

        connection(callback_t && callback, signal * signal)
            : m_callback(std::forward<callback_t>(callback))
            , m_signal(signal)
        {
            m_signal->m_connections.push_back(*this);
        }

        connection(connection && other)
            : m_signal(other.m_signal)
            , m_callback(std::move(other.m_callback))
        {
            replace(other);
        }

        connection & operator=(connection && other)
        {
            if (this != &other) {
                disconnect();
                m_signal = other.m_signal;
                m_callback = std::move(other.m_callback);
                replace(other);
            }
            return *this;
        }

        void replace(connection & other)
        {
            if (!m_signal) {
                other.disconnect();
                return;
            }

            auto other_iterator = m_signal->m_connections.as_iterator(other);
            auto this_iterator = m_signal->m_connections.insert(other_iterator, *this);

            auto current_token = m_signal->last_token;
            while (current_token) {
                if (current_token->iterator == other_iterator) {
                    current_token->iterator = this_iterator;
                }
                current_token = current_token->previous;
            }

            other.disconnect();
        }

        void disconnect() noexcept
        {
            if (!m_signal) {
                return;
            }

            for (auto token = m_signal->last_token; token; token = token->previous) {
                if (&*token->iterator == this) {
                    ++token->iterator;
                }
            }

            this->unlink();
            m_signal = nullptr;
            m_callback = {};
        }

        ~connection()
        {
            disconnect();
        }

    private:
        callback_t m_callback;
        signal * m_signal = nullptr;

        friend struct signal;
    };

    struct iteration_token
    {
        iteration_token(signal * signal)
            : m_signal(signal)
            , previous(signal->last_token)
            , iterator(signal->m_connections.begin())
        {
            m_signal->last_token = this;
        }

        ~iteration_token()
        {
            if (m_signal) {
                m_signal->last_token = previous;
            }
        }

        signal * m_signal = nullptr;
        iteration_token * previous;
        typename intrusive::list<connection, ConnectionTag>::const_iterator iterator;
    };

    signal() = default;

    signal(signal const &) = delete;
    signal & operator=(signal const &) = delete;

    connection connect(callback_t callback) noexcept
    {
        return connection(std::move(callback), this);
    }

    ~signal()
    {
        for (auto token = this->last_token; token; token = token->previous) {
            token->m_signal = nullptr;
        }
        while (!m_connections.empty()) {
            m_connections.back().disconnect();
        }
    }

    void operator()(Args... args) const
    {
        iteration_token token(const_cast<signal *>(this));
        while (token.m_signal && token.iterator != m_connections.end()) {
            (token.iterator++)->m_callback(args...);
        }
    }

private:
    intrusive::list<connection, ConnectionTag> m_connections;
    mutable iteration_token * last_token = nullptr;
};

} // namespace signals