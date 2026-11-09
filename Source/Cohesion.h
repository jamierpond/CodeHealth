#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <string_view>
#include <vector>

struct TSNode;

namespace CodeHealth
{
struct Language;

// A class and how many unrelated groups its methods fall into (LCOM4, Hitz and
// Montazeri 1995): methods are related when they use the same field or one
// calls the other.
struct ClassMetrics
{
    std::string name;
    int startLine = 0;
    int methodCount = 0;
    int fieldCount = 0;
    int cohesionGroups = 0;

    MIRO_REFLECT(name, startLine, methodCount, fieldCount, cohesionGroups)
};

std::vector<ClassMetrics> measureClassCohesion(const TSNode& root,
                                               std::string_view source,
                                               const Language& language);

// LCOM4 of a whole module (Python): how many unrelated groups its top-level
// functions and its classes' methods fall into.
int measureModuleCohesionGroups(const TSNode& root,
                                std::string_view source,
                                const Language& language);
} // namespace CodeHealth
