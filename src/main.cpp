#include <exception>
#include <iostream>

#include "npr/Application.hpp"

int main()
{
    try {
        npr::Application application;
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
}
