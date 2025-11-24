#include <Application.hpp>
#include <iostream>

int main()
{
    Application app;

    if (!app.initialize())
    {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    return app.run();
}
