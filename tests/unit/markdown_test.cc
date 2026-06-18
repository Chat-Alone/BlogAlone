#include "util/markdown.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using blogalone::util::render_markdown;

TEST(MarkdownTest, RendersBasicEmphasis)
{
    const auto html = render_markdown("Hello **world** and *stars*");
    EXPECT_NE(html.find("<strong>world</strong>"), std::string::npos);
    EXPECT_NE(html.find("<em>stars</em>"), std::string::npos);
}

TEST(MarkdownTest, DropsRawHtmlBlock)
{
    const auto html = render_markdown("<div onclick=\"x\">danger</div>\n\nsafe text");
    EXPECT_EQ(html.find("<div"), std::string::npos);
    EXPECT_EQ(html.find("onclick"), std::string::npos);
    EXPECT_NE(html.find("safe text"), std::string::npos);
}

TEST(MarkdownTest, DropsInlineHtml)
{
    const auto html = render_markdown("text <script>alert(1)</script> tail");
    EXPECT_EQ(html.find("<script"), std::string::npos);
    EXPECT_EQ(html.find("</script"), std::string::npos);
    EXPECT_NE(html.find("tail"), std::string::npos);
}

TEST(MarkdownTest, RejectsJavascriptLink)
{
    const auto html = render_markdown("[click](javascript:alert(1))");
    EXPECT_EQ(html.find("javascript:"), std::string::npos);
    EXPECT_EQ(html.find("<a "), std::string::npos);
    EXPECT_NE(html.find("click"), std::string::npos);
}

TEST(MarkdownTest, KeepsHttpLinkWithRel)
{
    const auto html = render_markdown("[site](https://example.com)");
    EXPECT_NE(html.find("href=\"https://example.com\""), std::string::npos);
    EXPECT_NE(html.find("rel=\"nofollow noopener noreferrer\""), std::string::npos);
}

TEST(MarkdownTest, KeepsMailtoLinkWithoutRel)
{
    const auto html = render_markdown("[mail](mailto:a@example.com)");
    EXPECT_NE(html.find("href=\"mailto:a@example.com\""), std::string::npos);
    EXPECT_EQ(html.find("rel="), std::string::npos);
}

TEST(MarkdownTest, DropsImagesWithoutPredicate)
{
    const auto html = render_markdown("![alt](/uploads/2026/06/aa/x.png)");
    EXPECT_EQ(html.find("<img"), std::string::npos);
}

TEST(MarkdownTest, KeepsImageAcceptedByPredicate)
{
    const auto html = render_markdown(
        "![alt](/uploads/2026/06/aa/x.png)",
        [](std::string_view url) {
            return url == "/uploads/2026/06/aa/x.png";
        }
    );
    EXPECT_NE(html.find("<img"), std::string::npos);
    EXPECT_NE(html.find("src=\"/uploads/2026/06/aa/x.png\""), std::string::npos);
}

TEST(MarkdownTest, RejectsImageRejectedByPredicate)
{
    const auto html = render_markdown(
        "![alt](https://evil.example.com/x.png)",
        [](std::string_view) { return false; }
    );
    EXPECT_EQ(html.find("<img"), std::string::npos);
}

TEST(MarkdownTest, DemotesHeadingsToParagraphs)
{
    const auto html = render_markdown("# Title\n\nbody");
    EXPECT_EQ(html.find("<h1"), std::string::npos);
    EXPECT_NE(html.find("<p>Title</p>"), std::string::npos);
}

TEST(MarkdownTest, DropsThematicBreak)
{
    const auto html = render_markdown("a\n\n---\n\nb");
    EXPECT_EQ(html.find("<hr"), std::string::npos);
}

TEST(MarkdownTest, KeepsCodeBlock)
{
    const auto html = render_markdown("```\ncode line\n```");
    EXPECT_NE(html.find("<pre>"), std::string::npos);
    EXPECT_NE(html.find("<code>"), std::string::npos);
    EXPECT_NE(html.find("code line"), std::string::npos);
}

TEST(MarkdownTest, EscapesTextSpecialCharacters)
{
    const auto html = render_markdown("a < b & c > d");
    EXPECT_NE(html.find("&lt;"), std::string::npos);
    EXPECT_NE(html.find("&amp;"), std::string::npos);
    EXPECT_NE(html.find("&gt;"), std::string::npos);
}

}
