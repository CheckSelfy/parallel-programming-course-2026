#include <iostream>
#include "generator.hpp"
#include "phase0.hpp"
#include "phase1.hpp"

template <class T>
std::string_view class_to_string() {
    std::string_view name = __PRETTY_FUNCTION__;

    size_t start = name.find("T = ") + 4;
    size_t end = name.find_first_of(";]", start);
    return name.substr(start, end - start);
}

int main() {
    auto values = bench::generate();
    auto collector = phase1::MetricCollector();
    auto result = bench::measurePoint(collector, values, 16);
    std::cout << "Measuring " << class_to_string<decltype(collector)>() << "; res = " << result << " ops/sec" << std::endl;
    return 0;
}