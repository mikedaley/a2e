#pragma once

#include <string>

/**
 * Returns the path to the application's resources directory.
 * This works both when running from the build directory and from the .app bundle.
 *
 * @return The path to the resources directory
 */
std::string getResourcePath();

/**
 * Constructs a path to a resource file, ensuring it will work both when running from
 * the build directory and from the .app bundle.
 *
 * @param resourceName The name of the resource file or directory, e.g., "roms/342-0135-A-CD.bin"
 * @return The full path to the resource
 */
std::string getResourcePath(const std::string &resourceName);
