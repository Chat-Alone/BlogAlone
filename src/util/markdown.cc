#include "util/markdown.h"

#include <cmark-gfm.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace blogalone::util {
namespace {

struct DocumentDeleter {
    void operator()(cmark_node* node) const { cmark_node_free(node); }
};

struct IterDeleter {
    void operator()(cmark_iter* iter) const { cmark_iter_free(iter); }
};

struct RenderedDeleter {
    void operator()(char* buffer) const { cmark_get_default_mem_allocator()->free(buffer); }
};

using DocumentPtr = std::unique_ptr<cmark_node, DocumentDeleter>;
using IterPtr = std::unique_ptr<cmark_iter, IterDeleter>;
using RenderedPtr = std::unique_ptr<char, RenderedDeleter>;

[[nodiscard]] std::string_view node_url(cmark_node* node)
{
    const auto* url = cmark_node_get_url(node);
    return url == nullptr ? std::string_view{} : std::string_view{url};
}

[[nodiscard]] bool starts_with_ci(std::string_view value, std::string_view prefix)
{
    if(value.size() < prefix.size()) {
        return false;
    }
    return std::ranges::equal(value.substr(0, prefix.size()), prefix, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == right;
    });
}

[[nodiscard]] std::size_t find_ci(std::string_view haystack, std::string_view needle, std::size_t from = 0)
{
    if(needle.empty()) {
        return from;
    }
    if(haystack.size() < needle.size() || from > haystack.size() - needle.size()) {
        return std::string_view::npos;
    }
    const auto end = haystack.size() - needle.size() + 1;
    for(std::size_t i = from; i < end; ++i) {
        if(std::ranges::equal(haystack.substr(i, needle.size()), needle, [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
        })) {
            return i;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] bool is_allowed_link_url(std::string_view url)
{
    return starts_with_ci(url, "http://")
        || starts_with_ci(url, "https://")
        || starts_with_ci(url, "mailto:");
}

[[nodiscard]] std::vector<cmark_node*> collect_nodes(cmark_node* root, cmark_node_type type)
{
    std::vector<cmark_node*> nodes;
    const IterPtr iter{cmark_iter_new(root)};
    cmark_event_type event = CMARK_EVENT_NONE;
    while((event = cmark_iter_next(iter.get())) != CMARK_EVENT_DONE) {
        if(event != CMARK_EVENT_ENTER) {
            continue;
        }
        cmark_node* node = cmark_iter_get_node(iter.get());
        if(cmark_node_get_type(node) == type) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

void move_children(cmark_node* from, cmark_node* before_target, bool append_to)
{
    cmark_node* child = cmark_node_first_child(from);
    while(child != nullptr) {
        cmark_node* next = cmark_node_next(child);
        cmark_node_unlink(child);
        if(append_to) {
            cmark_node_append_child(before_target, child);
        } else {
            cmark_node_insert_before(before_target, child);
        }
        child = next;
    }
}

void demote_headings(cmark_node* root)
{
    for(cmark_node* heading : collect_nodes(root, CMARK_NODE_HEADING)) {
        cmark_node* paragraph = cmark_node_new(CMARK_NODE_PARAGRAPH);
        move_children(heading, paragraph, true);
        cmark_node_own(paragraph);
        cmark_node_replace(heading, paragraph);
        cmark_node_free(heading);
    }
}

void unwrap_unsafe_links(cmark_node* root)
{
    for(cmark_node* link : collect_nodes(root, CMARK_NODE_LINK)) {
        if(is_allowed_link_url(node_url(link))) {
            continue;
        }
        move_children(link, link, false);
        cmark_node_unlink(link);
        cmark_node_free(link);
    }
}

void filter_images(cmark_node* root, const ImageUrlPredicate& image_url_allowed)
{
    for(cmark_node* image : collect_nodes(root, CMARK_NODE_IMAGE)) {
        if(image_url_allowed && image_url_allowed(node_url(image))) {
            continue;
        }
        cmark_node_unlink(image);
        cmark_node_free(image);
    }
}

void drop_nodes(cmark_node* root, cmark_node_type type)
{
    for(cmark_node* node : collect_nodes(root, type)) {
        cmark_node_unlink(node);
        cmark_node_free(node);
    }
}

[[nodiscard]] std::string add_external_link_rel(std::string_view html)
{
    constexpr std::string_view tag_open{"<a "};
    constexpr std::string_view href_attr{"href=\""};
    constexpr std::string_view http_scheme{"http"};
    constexpr std::string_view rel_attr{"rel=\"nofollow noopener noreferrer\" "};

    std::string output;
    output.reserve(html.size());
    std::size_t cursor = 0;
    while(cursor < html.size()) {
        const auto tag_pos = find_ci(html, tag_open, cursor);
        if(tag_pos == std::string_view::npos) {
            output.append(html.substr(cursor));
            break;
        }
        const auto after_tag = tag_pos + tag_open.size();
        const auto href_pos = find_ci(html, href_attr, after_tag);
        const auto next_tag = html.find('<', after_tag);
        if(href_pos == std::string_view::npos
            || (next_tag != std::string_view::npos && href_pos >= next_tag)) {
            output.append(html.substr(cursor, after_tag - cursor));
            cursor = after_tag;
            continue;
        }
        const auto scheme_start = href_pos + href_attr.size();
        if(scheme_start + http_scheme.size() > html.size()) {
            output.append(html.substr(cursor));
            break;
        }
        if(!starts_with_ci(html.substr(scheme_start, http_scheme.size()), http_scheme)) {
            output.append(html.substr(cursor, after_tag - cursor));
            cursor = after_tag;
            continue;
        }
        output.append(html.substr(cursor, tag_pos - cursor));
        output.append("<a ");
        output.append(rel_attr);
        cursor = after_tag;
    }
    return output;
}

}

std::string render_markdown(std::string_view markdown, const ImageUrlPredicate& image_url_allowed)
{
    const DocumentPtr document{
        cmark_parse_document(markdown.data(), markdown.size(), CMARK_OPT_VALIDATE_UTF8)
    };
    if(!document) {
        return {};
    }

    cmark_node* root = document.get();
    demote_headings(root);
    unwrap_unsafe_links(root);
    filter_images(root, image_url_allowed);
    drop_nodes(root, CMARK_NODE_HTML_BLOCK);
    drop_nodes(root, CMARK_NODE_HTML_INLINE);
    drop_nodes(root, CMARK_NODE_THEMATIC_BREAK);

    const RenderedPtr rendered{cmark_render_html(root, CMARK_OPT_DEFAULT, nullptr)};
    if(!rendered) {
        return {};
    }
    return add_external_link_rel(rendered.get());
}

std::string render_markdown(std::string_view markdown)
{
    return render_markdown(markdown, ImageUrlPredicate{});
}

}
