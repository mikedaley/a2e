#include <application.hpp>
#include <iostream>

int main()
{
    application app;

    if (!app.initialize())
    {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    return app.run();
}
