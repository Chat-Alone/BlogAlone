#include "models/user.h"

#include <stdexcept>

namespace blogalone::models {

std::string_view to_string(UserRole role)
{
    switch(role) {
    case UserRole::user:
        return "user";
    case UserRole::admin:
        return "admin";
    }
    return "user";
}

UserRole user_role_from_string(std::string_view value)
{
    if(value == "admin") {
        return UserRole::admin;
    }
    if(value == "user") {
        return UserRole::user;
    }
    throw std::invalid_argument{"invalid user role"};
}

}
