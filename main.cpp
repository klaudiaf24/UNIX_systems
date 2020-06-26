#include <iostream>
#include <unistd.h>
#include "list.hpp"

int main() {
	std::string name = "someName";
	std::size_t size = 40;
	auto pid = fork();
	if (!pid) {
		auto list = List::newList(name, size);
        std::cout << "========== Create list ===========\n";
        std::cout << "Push back:\n5\n10\n15\n";
		list.push_back(5);
        list.push_back(10);
		list.push_back(15);
		sleep(5);
	}
	else {
        sleep(1);
        auto list = List::useList(name, size);
        std::cout << "\n========== Use created list ===========\n";
        std::cout << "Head position -> " << &(*list.begin()) << '\n';
        std::cout << "Second item position -> " << &(*(++list.begin())) << '\n';
        std::cout << "\nWhole list:\n";
        for (auto&& iter : list)
            std::cout << iter << '\n';

        list.swap_in_memory(list.begin(), ++list.begin());

        std::cout << "\n========== Swap list ===========\n";
        std::cout << "Head position -> " << &(*list.begin()) << '\n';
        std::cout << "Second item position -> " << &(*(++list.begin())) << '\n';
        std::cout << "\nWhole list:\n";
        for (auto&& iter : list)
            std::cout << iter << '\n';
        std::cout << '\n';
	}
}
