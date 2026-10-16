#pragma once
#include <string>
namespace deck {
// A short, printable DNS-style APN; excludes AT command delimiters.
bool validApn(const std::string& value);
}
