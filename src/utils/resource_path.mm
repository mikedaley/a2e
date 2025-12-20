#include "utils/resource_path.hpp"
#include <string>
#include <iostream>
#include <filesystem>
#include <Foundation/Foundation.h>

std::string getResourcePath() {
    @autoreleasepool {
        NSBundle* mainBundle = [NSBundle mainBundle];

        // Check if we're running from an app bundle
        if (mainBundle && [mainBundle.bundlePath hasSuffix:@".app"]) {
            // We're running from a .app bundle
            NSString* resourcePath = [mainBundle resourcePath];
            return std::string([resourcePath UTF8String]) + "/";
        } else {
            // We're running from a development environment
            // Check if the include/roms directory exists at the expected relative path
            if (std::filesystem::exists("../include/roms")) {
                return "../";
            } else if (std::filesystem::exists("./include/roms")) {
                return "./";
            } else if (std::filesystem::exists("include/roms")) {
                return "./";
            } else {
                std::cerr << "Warning: Could not find include/roms directory. Using current directory." << std::endl;
                return "./";
            }
        }
    }
}

std::string getResourcePath(const std::string& resourceName) {
    return getResourcePath() + resourceName;
}
