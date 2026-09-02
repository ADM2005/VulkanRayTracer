#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

struct DeletionQueue {
	std::vector<std::function<void()>> _queue;

	void push(std::function<void()> func) {
		_queue.push_back(func);
	}

	void flush() {
		for (auto it = _queue.rbegin(); it != _queue.rend(); it++) {
			(*it)();
		}

		_queue.clear();
	}
};