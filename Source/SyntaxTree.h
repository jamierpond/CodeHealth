#pragma once

#include <tree_sitter/api.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CodeHealth
{
// Walks the children through a cursor: ts_node_child(node, index) is linear
// in index, so an index loop is quadratic on a node with many children.
template <typename Visitor>
inline void forEachChild(TSNode node, Visitor&& visitor)
{
    auto cursor = ts_tree_cursor_new(node);

    if (ts_tree_cursor_goto_first_child(&cursor))
    {
        do
        {
            visitor(ts_tree_cursor_current_node(&cursor));
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }

    ts_tree_cursor_delete(&cursor);
}

template <typename Visitor>
inline void forEachNamedChild(TSNode node, Visitor&& visitor)
{
    forEachChild(node,
                 [&](TSNode child)
                 {
                     if (ts_node_is_named(child))
                         visitor(child);
                 });
}

// Visits root and everything under it depth first, in source order, without
// recursion: a worker thread's stack is small and generated sources nest deep.
// The visitor gets each node with its parent's context and returns the context
// for its children, or nothing to skip them.
template <typename Context, typename Visitor>
void walkTree(TSNode root, Context rootContext, Visitor&& visitor)
{
    auto pending =
        std::vector<std::pair<TSNode, Context>> {{root, std::move(rootContext)}};

    while (!pending.empty())
    {
        auto [node, context] = std::move(pending.back());
        pending.pop_back();

        auto childContext = visitor(node, context);

        if (!childContext)
            continue;

        auto firstChild = pending.size();
        forEachChild(
            node, [&](TSNode child) { pending.emplace_back(child, *childContext); });
        std::reverse(pending.begin() + static_cast<std::ptrdiff_t>(firstChild),
                     pending.end());
    }
}

inline std::string_view getNodeType(TSNode node)
{
    return ts_node_type(node);
}

inline TSNode getField(TSNode node, std::string_view field)
{
    return ts_node_child_by_field_name(
        node, field.data(), static_cast<uint32_t>(field.size()));
}

inline bool hasField(TSNode node, std::string_view field)
{
    return !ts_node_is_null(getField(node, field));
}

inline std::string getNodeText(TSNode node, std::string_view source)
{
    auto start = ts_node_start_byte(node);
    auto end = ts_node_end_byte(node);
    return std::string {source.substr(start, end - start)};
}
} // namespace CodeHealth
