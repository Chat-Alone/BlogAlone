#include "app/routes.h"
#include "plugins/database_migration_plugin.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/value.h>
#include <sodium.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

class TempWorkspace {
  public:
    TempWorkspace()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        for(int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base
                / ("blogalone-http-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error{"unable to create integration test directory"};
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

struct HttpResult {
    drogon::ReqResult result;
    drogon::HttpResponsePtr response;
};

struct AuthSession {
    std::int64_t user_id;
    std::string session_token;
    std::string csrf_token;
};

class IntegrationServer final : public testing::Environment {
  public:
    void SetUp() override
    {
        database_path_ = workspace_.path() / "blogalone.db";
        const auto config = make_config();

        blogalone::plugins::ensure_database_migration_plugin_registered();
        drogon::app().loadConfigJson(config);
        blogalone::register_routes();

        auto started = std::make_shared<std::promise<std::uint16_t>>();
        auto ready = started->get_future();
        drogon::app().registerBeginningAdvice([started]() {
            const auto listeners = drogon::app().getListeners();
            if(listeners.empty()) {
                started->set_exception(
                    std::make_exception_ptr(std::runtime_error{"Drogon has no active listener"})
                );
                return;
            }
            started->set_value(listeners.front().toPort());
        });

        server_thread_ = std::thread{[]() { drogon::app().run(); }};
        if(ready.wait_for(std::chrono::seconds{10}) != std::future_status::ready) {
            drogon::app().quit();
            server_thread_.join();
            throw std::runtime_error{"Drogon integration server startup timed out"};
        }

        port_ = ready.get();
        base_url_ = "http://127.0.0.1:" + std::to_string(port_);
        client_ = drogon::HttpClient::newHttpClient(base_url_);
    }

    void TearDown() override
    {
        client_.reset();
        drogon::app().quit();
        if(server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    [[nodiscard]] HttpResult send(const drogon::HttpRequestPtr& request) const
    {
        const auto [result, response] = client_->sendRequest(request, 10.0);
        return HttpResult{.result = result, .response = response};
    }

    [[nodiscard]] const std::string& base_url() const
    {
        return base_url_;
    }

    [[nodiscard]] const std::filesystem::path& database_path() const
    {
        return database_path_;
    }

  private:
    [[nodiscard]] Json::Value make_config() const
    {
        Json::Value listener;
        listener["address"] = "127.0.0.1";
        listener["port"] = 0;
        listener["https"] = false;

        Json::Value database;
        database["name"] = "default";
        database["rdbms"] = "sqlite3";
        database["filename"] = database_path_.generic_string();
        database["is_fast"] = false;
        database["number_of_connections"] = 1;

        Json::Value plugin_config;
        plugin_config["database_path"] = database_path_.generic_string();
        plugin_config["migrations_dir"] =
            (std::filesystem::path{BLOGALONE_SOURCE_DIR} / "migrations").generic_string();
        plugin_config["db_client"] = "default";

        Json::Value plugin;
        plugin["name"] = "blogalone::plugins::DatabaseMigrationPlugin";
        plugin["dependencies"] = Json::arrayValue;
        plugin["config"] = std::move(plugin_config);

        Json::Value custom;
        custom["trusted_proxies"] = Json::arrayValue;
        custom["web_root"] =
            (std::filesystem::path{BLOGALONE_SOURCE_DIR} / "web").generic_string();
        custom["uploads_root"] = (workspace_.path() / "uploads").generic_string();
        custom["password_opslimit"] = Json::UInt64{crypto_pwhash_OPSLIMIT_MIN};
        custom["password_memlimit"] = Json::UInt64{crypto_pwhash_MEMLIMIT_MIN};
        // All TEST cases share this one server process for the whole binary
        // lifetime, so the process-wide registration rate limiter must not
        // use the production default (5/hour): the suite registers more
        // accounts than that across its test cases. Login keeps the
        // production default because SuccessfulLoginDoesNotResetFailedLoginLimit
        // asserts on that exact threshold.
        custom["rate_limit_registration_max_requests"] = 1'000;

        Json::Value config;
        config["listeners"].append(std::move(listener));
        config["app"]["threads_num"] = 2;
        config["app"]["run_as_daemon"] = false;
        config["db_clients"].append(std::move(database));
        config["plugins"].append(std::move(plugin));
        config["custom_config"] = std::move(custom);
        return config;
    }

    TempWorkspace workspace_;
    std::filesystem::path database_path_;
    std::uint16_t port_{};
    std::string base_url_;
    drogon::HttpClientPtr client_;
    std::thread server_thread_;
};

IntegrationServer* server{};

[[nodiscard]] drogon::HttpRequestPtr request_for(
    drogon::HttpMethod method,
    std::string path
)
{
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(method);
    request->setPath(std::move(path));
    return request;
}

[[nodiscard]] drogon::HttpRequestPtr json_request_for(
    drogon::HttpMethod method,
    std::string path,
    Json::Value body
)
{
    auto request = drogon::HttpRequest::newHttpJsonRequest(std::move(body));
    request->setMethod(method);
    request->setPath(std::move(path));
    return request;
}

void expect_ok(const HttpResult& result)
{
    ASSERT_EQ(result.result, drogon::ReqResult::Ok);
    ASSERT_NE(result.response, nullptr);
}

[[nodiscard]] const Json::Value& json_body(const drogon::HttpResponsePtr& response)
{
    const auto json = response->getJsonObject();
    if(!json) {
        throw std::runtime_error{"response body is not JSON"};
    }
    return *json;
}

void add_auth(
    const drogon::HttpRequestPtr& request,
    std::string_view session_token,
    std::string_view csrf_token
)
{
    request->addCookie("ba_session", std::string{session_token});
    request->addCookie("ba_csrf", std::string{csrf_token});
    request->addHeader("X-CSRF-Token", std::string{csrf_token});
    request->addHeader("Origin", server->base_url());
}

[[nodiscard]] AuthSession register_user(
    std::string username,
    std::string email,
    std::string password
)
{
    Json::Value body;
    body["username"] = std::move(username);
    body["email"] = std::move(email);
    body["password"] = password;
    const auto registered = server->send(json_request_for(
        drogon::Post,
        "/api/auth/register",
        std::move(body)
    ));
    if(registered.result != drogon::ReqResult::Ok || !registered.response
        || registered.response->statusCode() != drogon::k200OK) {
        throw std::runtime_error{"user registration failed"};
    }

    const auto& session_cookie = registered.response->getCookie("ba_session");
    const auto& csrf_cookie = registered.response->getCookie("ba_csrf");
    if(!session_cookie || !csrf_cookie) {
        throw std::runtime_error{"registration did not issue authentication cookies"};
    }
    return AuthSession{
        .user_id = json_body(registered.response)["user"]["id"].asInt64(),
        .session_token = session_cookie.getValue(),
        .csrf_token = csrf_cookie.getValue()
    };
}

[[nodiscard]] drogon::HttpRequestPtr authenticated_json_request(
    drogon::HttpMethod method,
    std::string path,
    Json::Value body,
    const AuthSession& session
)
{
    auto request = json_request_for(method, std::move(path), std::move(body));
    add_auth(request, session.session_token, session.csrf_token);
    return request;
}

[[nodiscard]] std::vector<unsigned char> valid_1x1_png()
{
    return {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
        0x89, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47,
        0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00,
        0x00, 0x04, 0x67, 0x41, 0x4d, 0x41, 0x00, 0x00,
        0xb1, 0x8f, 0x0b, 0xfc, 0x61, 0x05, 0x00, 0x00,
        0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00,
        0x0e, 0xc3, 0x00, 0x00, 0x0e, 0xc3, 0x01, 0xc7,
        0x6f, 0xa8, 0x64, 0x00, 0x00, 0x00, 0x0d, 0x49,
        0x44, 0x41, 0x54, 0x18, 0x57, 0x63, 0xf8, 0xcf,
        0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0xa6, 0x5c, 0x9b, 0x5d, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };
}

TEST(HttpIntegrationTest, ServesPagesStaticResourcesAndJsonErrors)
{
    const auto page = server->send(request_for(drogon::Get, "/"));
    expect_ok(page);
    EXPECT_EQ(page.response->statusCode(), drogon::k200OK);
    EXPECT_TRUE(page.response->contentTypeString().starts_with("text/html"));
    EXPECT_NE(page.response->body().find("BlogAlone"), std::string_view::npos);

    const auto thread_page = server->send(request_for(drogon::Get, "/threads/42"));
    expect_ok(thread_page);
    EXPECT_EQ(thread_page.response->statusCode(), drogon::k200OK);

    const auto stylesheet = server->send(request_for(drogon::Get, "/static/css/base.css"));
    expect_ok(stylesheet);
    EXPECT_EQ(stylesheet.response->statusCode(), drogon::k200OK);
    EXPECT_TRUE(stylesheet.response->contentTypeString().starts_with("text/css"));

    const auto missing = server->send(request_for(drogon::Get, "/missing"));
    expect_ok(missing);
    EXPECT_EQ(missing.response->statusCode(), drogon::k404NotFound);
    const auto& error = json_body(missing.response)["error"];
    EXPECT_EQ(error["code"].asString(), "not_found");
    EXPECT_TRUE(error["request_id"].asString().starts_with("req_"));
    EXPECT_FALSE(missing.response->getHeader("X-Request-Id").empty());
}

TEST(HttpIntegrationTest, EnforcesCookiesCsrfAndPersistsForumWrites)
{
    Json::Value registration;
    registration["username"] = "integration_user";
    registration["email"] = "integration@example.com";
    registration["password"] = "correct-horse-battery-staple";
    const auto registered = server->send(json_request_for(
        drogon::Post,
        "/api/auth/register",
        std::move(registration)
    ));
    expect_ok(registered);
    ASSERT_EQ(registered.response->statusCode(), drogon::k200OK);

    const auto& session_cookie = registered.response->getCookie("ba_session");
    const auto& csrf_cookie = registered.response->getCookie("ba_csrf");
    ASSERT_TRUE(session_cookie);
    ASSERT_TRUE(csrf_cookie);
    EXPECT_TRUE(session_cookie.isHttpOnly());
    EXPECT_FALSE(csrf_cookie.isHttpOnly());
    EXPECT_TRUE(session_cookie.isSecure());
    EXPECT_TRUE(csrf_cookie.isSecure());
    EXPECT_EQ(session_cookie.getPath(), "/");
    EXPECT_EQ(session_cookie.getSameSite(), drogon::Cookie::SameSite::kLax);
    EXPECT_EQ(csrf_cookie.getSameSite(), drogon::Cookie::SameSite::kLax);
    EXPECT_EQ(session_cookie.getMaxAge(), 1'209'600);
    EXPECT_EQ(csrf_cookie.getMaxAge(), 1'209'600);

    auto me_request = request_for(drogon::Get, "/api/me");
    me_request->addCookie("ba_session", session_cookie.getValue());
    const auto me = server->send(me_request);
    expect_ok(me);
    ASSERT_EQ(me.response->statusCode(), drogon::k200OK);
    EXPECT_EQ(json_body(me.response)["user"]["username"].asString(), "integration_user");

    drogon::app().getDbClient()->execSqlSync(
        "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
        "VALUES ('integration', 'Integration', '', 0, 1, 1)"
    );
    Json::Value rejected_body;
    rejected_body["forum_slug"] = "integration";
    rejected_body["title"] = "Rejected thread";
    rejected_body["body_md"] = "missing csrf";
    auto rejected_request = json_request_for(
        drogon::Post,
        "/api/threads",
        std::move(rejected_body)
    );
    rejected_request->addCookie("ba_session", session_cookie.getValue());
    const auto rejected = server->send(rejected_request);
    expect_ok(rejected);
    ASSERT_EQ(rejected.response->statusCode(), drogon::k403Forbidden);
    EXPECT_EQ(json_body(rejected.response)["error"]["code"].asString(), "forbidden");

    Json::Value thread_body;
    thread_body["forum_slug"] = "integration";
    thread_body["title"] = "Integration thread";
    thread_body["body_md"] = "**persisted** body";
    auto create_request = json_request_for(
        drogon::Post,
        "/api/threads",
        std::move(thread_body)
    );
    add_auth(create_request, session_cookie.getValue(), csrf_cookie.getValue());
    const auto created = server->send(create_request);
    expect_ok(created);
    ASSERT_EQ(created.response->statusCode(), drogon::k201Created);
    const auto thread_id = json_body(created.response)["thread"]["id"].asInt64();
    EXPECT_GT(thread_id, 0);

    Json::Value post_body;
    post_body["body_md"] = "floor reply";
    auto post_request = json_request_for(
        drogon::Post,
        "/api/threads/" + std::to_string(thread_id) + "/posts",
        std::move(post_body)
    );
    add_auth(post_request, session_cookie.getValue(), csrf_cookie.getValue());
    const auto post = server->send(post_request);
    expect_ok(post);
    ASSERT_EQ(post.response->statusCode(), drogon::k201Created);
    const auto post_id = json_body(post.response)["post"]["id"].asInt64();
    EXPECT_EQ(json_body(post.response)["post"]["floor_no"].asInt64(), 1);

    Json::Value sub_post_body;
    sub_post_body["body_md"] = "nested reply";
    auto sub_post_request = json_request_for(
        drogon::Post,
        "/api/posts/" + std::to_string(post_id) + "/sub_posts",
        std::move(sub_post_body)
    );
    add_auth(sub_post_request, session_cookie.getValue(), csrf_cookie.getValue());
    const auto sub_post = server->send(sub_post_request);
    expect_ok(sub_post);
    ASSERT_EQ(sub_post.response->statusCode(), drogon::k201Created);
    EXPECT_EQ(json_body(sub_post.response)["sub_post"]["body_md"].asString(), "nested reply");

    const auto detail = server->send(request_for(
        drogon::Get,
        "/api/threads/" + std::to_string(thread_id)
    ));
    expect_ok(detail);
    ASSERT_EQ(detail.response->statusCode(), drogon::k200OK);
    EXPECT_EQ(json_body(detail.response)["posts"]["total"].asInt64(), 1);
    ASSERT_EQ(json_body(detail.response)["posts"]["items"].size(), 1);
    EXPECT_EQ(json_body(detail.response)["posts"]["items"][0]["sub_posts"].size(), 1);

    const auto rows = drogon::app().getDbClient()->execSqlSync(
        "SELECT title, body_html FROM threads WHERE id = ?",
        thread_id
    );
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.at(0)["title"].as<std::string>(), "Integration thread");
    EXPECT_NE(rows.at(0)["body_html"].as<std::string>().find("<strong>persisted</strong>"), std::string::npos);

    const auto invalid_page = server->send(request_for(
        drogon::Get,
        "/api/forums/integration/threads?page=0"
    ));
    expect_ok(invalid_page);
    ASSERT_EQ(invalid_page.response->statusCode(), drogon::k400BadRequest);
    EXPECT_EQ(json_body(invalid_page.response)["error"]["code"].asString(), "invalid_argument");

    auto logout_request = request_for(drogon::Post, "/api/auth/logout");
    add_auth(logout_request, session_cookie.getValue(), csrf_cookie.getValue());
    const auto logout = server->send(logout_request);
    expect_ok(logout);
    EXPECT_EQ(logout.response->statusCode(), drogon::k204NoContent);
    EXPECT_EQ(logout.response->getCookie("ba_session").getMaxAge(), 0);

    auto expired_me_request = request_for(drogon::Get, "/api/me");
    expired_me_request->addCookie("ba_session", session_cookie.getValue());
    const auto expired_me = server->send(expired_me_request);
    expect_ok(expired_me);
    ASSERT_EQ(expired_me.response->statusCode(), drogon::k401Unauthorized);
    EXPECT_EQ(json_body(expired_me.response)["error"]["code"].asString(), "unauthenticated");

    Json::Value login_body;
    login_body["username"] = "integration_user";
    login_body["password"] = "correct-horse-battery-staple";
    const auto login = server->send(json_request_for(
        drogon::Post,
        "/api/auth/login",
        std::move(login_body)
    ));
    expect_ok(login);
    ASSERT_EQ(login.response->statusCode(), drogon::k200OK);
    EXPECT_TRUE(login.response->getCookie("ba_session"));
    EXPECT_NE(
        login.response->getCookie("ba_session").getValue(),
        session_cookie.getValue()
    );
}

TEST(HttpIntegrationTest, UploadsDownloadsAndEditsThreadWithImage)
{
    const auto session = register_user(
        "image_user",
        "image@example.com",
        "correct-horse-battery-staple"
    );
    drogon::app().getDbClient()->execSqlSync(
        "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
        "VALUES ('images', 'Images', '', 0, 1, 1)"
    );

    const auto png = valid_1x1_png();
    const drogon::UploadFile file{
        png.data(),
        png.size(),
        "pixel.png",
        "file",
        drogon::CT_IMAGE_PNG
    };
    auto upload_request = drogon::HttpRequest::newFileUploadRequest({file});
    upload_request->setMethod(drogon::Post);
    upload_request->setPath("/api/uploads");
    add_auth(upload_request, session.session_token, session.csrf_token);
    const auto uploaded = server->send(upload_request);
    expect_ok(uploaded);
    ASSERT_EQ(uploaded.response->statusCode(), drogon::k201Created);
    const auto upload_url = json_body(uploaded.response)["upload"]["url"].asString();
    EXPECT_TRUE(upload_url.starts_with("/uploads/"));
    EXPECT_EQ(json_body(uploaded.response)["upload"]["mime"].asString(), "image/png");
    EXPECT_EQ(json_body(uploaded.response)["upload"]["width"].asInt64(), 1);
    EXPECT_EQ(json_body(uploaded.response)["upload"]["height"].asInt64(), 1);

    const auto downloaded = server->send(request_for(drogon::Get, upload_url));
    expect_ok(downloaded);
    ASSERT_EQ(downloaded.response->statusCode(), drogon::k200OK);
    EXPECT_TRUE(downloaded.response->contentTypeString().starts_with("image/png"));
    EXPECT_EQ(downloaded.response->getHeader("X-Content-Type-Options"), "nosniff");
    const std::string expected_image(png.begin(), png.end());
    EXPECT_EQ(downloaded.response->body(), expected_image);

    Json::Value create_body;
    create_body["forum_slug"] = "images";
    create_body["title"] = "Image thread";
    create_body["body_md"] = "![pixel](" + upload_url + ")";
    const auto created = server->send(authenticated_json_request(
        drogon::Post,
        "/api/threads",
        std::move(create_body),
        session
    ));
    expect_ok(created);
    ASSERT_EQ(created.response->statusCode(), drogon::k201Created);
    const auto thread_id = json_body(created.response)["thread"]["id"].asInt64();
    EXPECT_NE(json_body(created.response)["thread"]["body_html"].asString().find(upload_url), std::string::npos);

    Json::Value update_body;
    update_body["title"] = "Updated image thread";
    update_body["body_md"] = "updated ![pixel](" + upload_url + ")";
    const auto updated = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/threads/" + std::to_string(thread_id),
        std::move(update_body),
        session
    ));
    expect_ok(updated);
    ASSERT_EQ(updated.response->statusCode(), drogon::k200OK);
    EXPECT_NE(json_body(updated.response)["thread"]["body_html"].asString().find(upload_url), std::string::npos);

    const auto refs = drogon::app().getDbClient()->execSqlSync(
        "SELECT attached_at FROM upload_refs WHERE owner_id = ?",
        session.user_id
    );
    ASSERT_EQ(refs.size(), 1);
    EXPECT_FALSE(refs.at(0)["attached_at"].isNull());
}

TEST(HttpIntegrationTest, EnforcesAdminRoleAndPersistsPrivilegedActions)
{
    const auto admin = register_user(
        "integration_admin",
        "admin@example.com",
        "correct-horse-battery-staple"
    );
    const auto target = register_user(
        "integration_target",
        "target@example.com",
        "correct-horse-battery-staple"
    );

    auto forbidden_request = request_for(drogon::Get, "/api/admin/users");
    add_auth(forbidden_request, target.session_token, target.csrf_token);
    const auto forbidden = server->send(forbidden_request);
    expect_ok(forbidden);
    ASSERT_EQ(forbidden.response->statusCode(), drogon::k403Forbidden);

    drogon::app().getDbClient()->execSqlSync(
        "UPDATE users SET role = 'admin' WHERE id = ?",
        admin.user_id
    );
    Json::Value reauth_body;
    reauth_body["password"] = "correct-horse-battery-staple";
    const auto reauthenticated = server->send(authenticated_json_request(
        drogon::Post,
        "/api/admin/reauth",
        std::move(reauth_body),
        admin
    ));
    expect_ok(reauthenticated);
    ASSERT_EQ(reauthenticated.response->statusCode(), drogon::k200OK);
    EXPECT_GT(json_body(reauthenticated.response)["admin_confirmed_at"].asInt64(), 0);

    Json::Value forum_body;
    forum_body["slug"] = "admin-created";
    forum_body["name"] = "Admin Created";
    forum_body["description"] = "created through HTTP";
    forum_body["sort_order"] = 10;
    const auto forum = server->send(authenticated_json_request(
        drogon::Post,
        "/api/admin/forums",
        std::move(forum_body),
        admin
    ));
    expect_ok(forum);
    ASSERT_EQ(forum.response->statusCode(), drogon::k201Created);
    EXPECT_EQ(json_body(forum.response)["forum"]["slug"].asString(), "admin-created");

    Json::Value role_body;
    role_body["role"] = "admin";
    const auto promoted = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/admin/users/" + std::to_string(target.user_id) + "/role",
        std::move(role_body),
        admin
    ));
    expect_ok(promoted);
    ASSERT_EQ(promoted.response->statusCode(), drogon::k200OK);
    EXPECT_EQ(json_body(promoted.response)["user"]["role"].asString(), "admin");

    auto audit_request = request_for(drogon::Get, "/api/admin/audit_logs?page=1&page_size=20");
    add_auth(audit_request, admin.session_token, admin.csrf_token);
    const auto audit = server->send(audit_request);
    expect_ok(audit);
    ASSERT_EQ(audit.response->statusCode(), drogon::k200OK);
    EXPECT_GE(json_body(audit.response)["total"].asInt64(), 2);

    const auto users = drogon::app().getDbClient()->execSqlSync(
        "SELECT role FROM users WHERE id = ?",
        target.user_id
    );
    ASSERT_EQ(users.size(), 1);
    EXPECT_EQ(users.at(0)["role"].as<std::string>(), "admin");
    const auto audit_rows = drogon::app().getDbClient()->execSqlSync(
        "SELECT action FROM audit_log WHERE action = 'user.role.change' AND target_id = ?",
        target.user_id
    );
    EXPECT_EQ(audit_rows.size(), 1);
}

TEST(HttpIntegrationTest, ListsAdminSessionsAndDeletedContentThenReflectsRestore)
{
    const auto admin = register_user(
        "sessions_admin",
        "sessions_admin@example.com",
        "correct-horse-battery-staple"
    );
    const auto author = register_user(
        "sessions_author",
        "sessions_author@example.com",
        "correct-horse-battery-staple"
    );
    drogon::app().getDbClient()->execSqlSync(
        "UPDATE users SET role = 'admin' WHERE id = ?",
        admin.user_id
    );

    // Non-admins are rejected on every new read-only management route.
    auto forbidden_sessions = request_for(drogon::Get, "/api/admin/sessions");
    add_auth(forbidden_sessions, author.session_token, author.csrf_token);
    const auto forbidden_sessions_result = server->send(forbidden_sessions);
    expect_ok(forbidden_sessions_result);
    EXPECT_EQ(forbidden_sessions_result.response->statusCode(), drogon::k403Forbidden);

    auto forbidden_deleted = request_for(drogon::Get, "/api/admin/deleted/threads");
    add_auth(forbidden_deleted, author.session_token, author.csrf_token);
    const auto forbidden_deleted_result = server->send(forbidden_deleted);
    expect_ok(forbidden_deleted_result);
    EXPECT_EQ(forbidden_deleted_result.response->statusCode(), drogon::k403Forbidden);

    // Session listing surfaces both accounts' sessions without leaking tokens.
    auto sessions_request = request_for(drogon::Get, "/api/admin/sessions?page=1&page_size=20");
    add_auth(sessions_request, admin.session_token, admin.csrf_token);
    const auto sessions_result = server->send(sessions_request);
    expect_ok(sessions_result);
    ASSERT_EQ(sessions_result.response->statusCode(), drogon::k200OK);
    const auto& sessions_body = json_body(sessions_result.response);
    EXPECT_GE(sessions_body["total"].asInt64(), 2);
    for(const auto& item : sessions_body["items"]) {
        EXPECT_FALSE(item["token_hash"].asString().empty());
        EXPECT_FALSE(item.isMember("token"));
    }

    auto filtered_sessions = request_for(
        drogon::Get,
        "/api/admin/sessions?user_id=" + std::to_string(author.user_id)
    );
    add_auth(filtered_sessions, admin.session_token, admin.csrf_token);
    const auto filtered_sessions_result = server->send(filtered_sessions);
    expect_ok(filtered_sessions_result);
    ASSERT_EQ(filtered_sessions_result.response->statusCode(), drogon::k200OK);
    const auto& filtered_sessions_body = json_body(filtered_sessions_result.response);
    ASSERT_EQ(filtered_sessions_body["items"].size(), 1);
    EXPECT_EQ(filtered_sessions_body["items"][0]["username"].asString(), "sessions_author");

    // Deleted-content listings start empty, then reveal soft-deleted items with context.
    drogon::app().getDbClient()->execSqlSync(
        "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
        "VALUES ('deleted-listing', 'Deleted Listing', '', 0, 1, 1)"
    );
    Json::Value thread_body;
    thread_body["forum_slug"] = "deleted-listing";
    thread_body["title"] = "Thread pending deletion";
    thread_body["body_md"] = "original body";
    const auto thread_created = server->send(authenticated_json_request(
        drogon::Post,
        "/api/threads",
        std::move(thread_body),
        author
    ));
    expect_ok(thread_created);
    ASSERT_EQ(thread_created.response->statusCode(), drogon::k201Created);
    const auto thread_id = json_body(thread_created.response)["thread"]["id"].asInt64();

    Json::Value post_body;
    post_body["body_md"] = "reply pending deletion";
    const auto post_created = server->send(authenticated_json_request(
        drogon::Post,
        "/api/threads/" + std::to_string(thread_id) + "/posts",
        std::move(post_body),
        author
    ));
    expect_ok(post_created);
    ASSERT_EQ(post_created.response->statusCode(), drogon::k201Created);
    const auto post_id = json_body(post_created.response)["post"]["id"].asInt64();

    Json::Value sub_post_body;
    sub_post_body["body_md"] = "nested reply pending deletion";
    const auto sub_post_created = server->send(authenticated_json_request(
        drogon::Post,
        "/api/posts/" + std::to_string(post_id) + "/sub_posts",
        std::move(sub_post_body),
        author
    ));
    expect_ok(sub_post_created);
    ASSERT_EQ(sub_post_created.response->statusCode(), drogon::k201Created);
    const auto sub_post_id = json_body(sub_post_created.response)["sub_post"]["id"].asInt64();

    auto empty_deleted_threads = request_for(drogon::Get, "/api/admin/deleted/threads");
    add_auth(empty_deleted_threads, admin.session_token, admin.csrf_token);
    const auto empty_deleted_threads_result = server->send(empty_deleted_threads);
    expect_ok(empty_deleted_threads_result);
    EXPECT_EQ(json_body(empty_deleted_threads_result.response)["total"].asInt64(), 0);

    Json::Value delete_flag;
    delete_flag["is_deleted"] = true;
    const auto thread_deleted = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/admin/threads/" + std::to_string(thread_id) + "/delete",
        delete_flag,
        admin
    ));
    expect_ok(thread_deleted);
    ASSERT_EQ(thread_deleted.response->statusCode(), drogon::k200OK);
    const auto post_deleted = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/admin/posts/" + std::to_string(post_id) + "/delete",
        delete_flag,
        admin
    ));
    expect_ok(post_deleted);
    ASSERT_EQ(post_deleted.response->statusCode(), drogon::k200OK);
    const auto sub_post_deleted = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/admin/sub_posts/" + std::to_string(sub_post_id) + "/delete",
        delete_flag,
        admin
    ));
    expect_ok(sub_post_deleted);
    ASSERT_EQ(sub_post_deleted.response->statusCode(), drogon::k200OK);

    auto deleted_threads_request = request_for(drogon::Get, "/api/admin/deleted/threads");
    add_auth(deleted_threads_request, admin.session_token, admin.csrf_token);
    const auto deleted_threads_result = server->send(deleted_threads_request);
    expect_ok(deleted_threads_result);
    ASSERT_EQ(deleted_threads_result.response->statusCode(), drogon::k200OK);
    const auto& deleted_threads_body = json_body(deleted_threads_result.response);
    ASSERT_EQ(deleted_threads_body["items"].size(), 1);
    EXPECT_EQ(deleted_threads_body["items"][0]["title"].asString(), "Thread pending deletion");
    EXPECT_EQ(deleted_threads_body["items"][0]["forum"]["slug"].asString(), "deleted-listing");
    EXPECT_EQ(deleted_threads_body["items"][0]["deleted_by"]["username"].asString(), "sessions_admin");

    auto deleted_posts_request = request_for(drogon::Get, "/api/admin/deleted/posts");
    add_auth(deleted_posts_request, admin.session_token, admin.csrf_token);
    const auto deleted_posts_result = server->send(deleted_posts_request);
    expect_ok(deleted_posts_result);
    const auto& deleted_posts_body = json_body(deleted_posts_result.response);
    ASSERT_EQ(deleted_posts_body["items"].size(), 1);
    EXPECT_EQ(deleted_posts_body["items"][0]["floor_no"].asInt64(), 1);

    auto deleted_sub_posts_request = request_for(drogon::Get, "/api/admin/deleted/sub_posts");
    add_auth(deleted_sub_posts_request, admin.session_token, admin.csrf_token);
    const auto deleted_sub_posts_result = server->send(deleted_sub_posts_request);
    expect_ok(deleted_sub_posts_result);
    const auto& deleted_sub_posts_body = json_body(deleted_sub_posts_result.response);
    ASSERT_EQ(deleted_sub_posts_body["items"].size(), 1);
    EXPECT_EQ(deleted_sub_posts_body["items"][0]["thread_title"].asString(), "Thread pending deletion");

    // Restoring the thread removes it from the deleted-content listing again.
    Json::Value restore_flag;
    restore_flag["is_deleted"] = false;
    const auto thread_restored = server->send(authenticated_json_request(
        drogon::Patch,
        "/api/admin/threads/" + std::to_string(thread_id) + "/delete",
        restore_flag,
        admin
    ));
    expect_ok(thread_restored);
    ASSERT_EQ(thread_restored.response->statusCode(), drogon::k200OK);

    auto after_restore_request = request_for(drogon::Get, "/api/admin/deleted/threads");
    add_auth(after_restore_request, admin.session_token, admin.csrf_token);
    const auto after_restore_result = server->send(after_restore_request);
    expect_ok(after_restore_result);
    EXPECT_EQ(json_body(after_restore_result.response)["total"].asInt64(), 0);

    // Invalid pagination is rejected the same way as the other admin list routes.
    auto invalid_pagination = request_for(drogon::Get, "/api/admin/sessions?page=0");
    add_auth(invalid_pagination, admin.session_token, admin.csrf_token);
    const auto invalid_pagination_result = server->send(invalid_pagination);
    expect_ok(invalid_pagination_result);
    ASSERT_EQ(invalid_pagination_result.response->statusCode(), drogon::k400BadRequest);
    EXPECT_EQ(json_body(invalid_pagination_result.response)["error"]["code"].asString(), "invalid_argument");
}

TEST(HttpIntegrationTest, SuccessfulLoginDoesNotResetFailedLoginLimit)
{
    static_cast<void>(register_user(
        "rate_limit_owner",
        "rate-limit-owner@example.test",
        "correct-horse-battery-staple"
    ));

    const auto send_login = [](std::string username, std::string password) {
        Json::Value body;
        body["username"] = std::move(username);
        body["password"] = std::move(password);
        return server->send(json_request_for(
            drogon::Post,
            "/api/auth/login",
            std::move(body)
        ));
    };

    for(int attempt = 0; attempt < 4; ++attempt) {
        const auto failed = send_login("missing_rate_limit_user", "wrong-password");
        expect_ok(failed);
        ASSERT_EQ(failed.response->statusCode(), drogon::k401Unauthorized);
    }

    const auto successful = send_login(
        "rate_limit_owner",
        "correct-horse-battery-staple"
    );
    expect_ok(successful);
    ASSERT_EQ(successful.response->statusCode(), drogon::k200OK);

    const auto fifth_failure = send_login("missing_rate_limit_user", "wrong-password");
    expect_ok(fifth_failure);
    ASSERT_EQ(fifth_failure.response->statusCode(), drogon::k401Unauthorized);

    const auto limited = send_login("missing_rate_limit_user", "wrong-password");
    expect_ok(limited);
    EXPECT_EQ(limited.response->statusCode(), drogon::k429TooManyRequests);
}

}

int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    server = static_cast<IntegrationServer*>(
        testing::AddGlobalTestEnvironment(new IntegrationServer{})
    );
    return RUN_ALL_TESTS();
}
