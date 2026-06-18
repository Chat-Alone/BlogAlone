#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace blogalone::util {

using ImageUrlPredicate = std::function<bool(std::string_view)>;

// Renders untrusted Markdown into a safe HTML fragment. Raw HTML is dropped,
// link protocols are restricted to http/https/mailto, external links gain
// rel="nofollow noopener noreferrer", and images survive only when
// `image_url_allowed` accepts their source. Headings, thematic breaks and any
// non-whitelisted construct are reduced so the output stays within
// p/br/blockquote/ul/ol/li/strong/em/code/pre/a/img.
[[nodiscard]] std::string render_markdown(
    std::string_view markdown,
    const ImageUrlPredicate& image_url_allowed
);

[[nodiscard]] std::string render_markdown(std::string_view markdown);

}
