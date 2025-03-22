#ifndef MAX_SIZE_QUEUE_H
#define MAX_SIZE_QUEUE_H



#include <cstdint>
#include <deque>
#include <vector>

template <typename T>
class max_size_queue {
public:
  explicit max_size_queue(const uint32_t size)
    : m_size(size) {}
  bool empty() const;
  T& front() const;
  void pop();
  bool push(const T& value);
  std::vector<T> to_vector() const;
private:
  std::deque<T> m_queue;
  uint32_t m_size;
};

template <typename T>
bool max_size_queue<T>::empty() const {
  return m_queue.empty();
}

template <typename T>
T& max_size_queue<T>::front() const {
  return m_queue.front();
}

template <typename T>
void max_size_queue<T>::pop() {
  m_queue.pop();
}

template <typename T>
bool max_size_queue<T>::push(const T& value) {
  if (m_queue.size() == m_size) {
    return false;
  }
  m_queue.push(value);
  return true;
}

template <typename T>
std::vector<T> max_size_queue<T>::to_vector() const {
  std::vector<T> vec(m_queue.begin(), m_queue.end());
  return vec;
}



#endif //MAX_SIZE_QUEUE_H
