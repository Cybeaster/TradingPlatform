#define NOMINMAX

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <functional>
#include <iostream>
#include <json/json.h>
#include <string>
#include <thread>

using json = Json::Value;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::HttpStatusCode;

static void
respondJson(const std::function<void(const HttpResponsePtr &)> &callback,
            HttpStatusCode code, const json &body) {
  auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
  resp->setStatusCode(code);
  callback(resp);
}

// Глобальная строка подключения (заполним в main)
static std::string g_connStr;

// Ленивый DbClient: создаётся при первом обращении (пул = 1, меньше churn'a)
static drogon::orm::DbClientPtr getDb() {
  static auto db = drogon::orm::DbClient::newPgClient(g_connStr, 1);
  return db;
}

// Кроссплатформенный аккуратный выход
static void onSignal(int) { drogon::app().quit(); }

int main(int, char **) {
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  try {
    const char *dbEnv = std::getenv("DATABASE_URL");
    // Добавили connect_timeout=2 — чтобы не крутить длинные зависшие коннекты
    g_connStr = std::string(dbEnv);

    // ---------- /health (GET) ----------
    // Делает лёгкий запрос к БД, но если БД мертва — просто ставим degraded.
    drogon::app().registerHandler(
        "/health",
        [](const HttpRequestPtr &,
           std::function<void(const HttpResponsePtr &)> &&cb) {
          auto db = getDb();
          db->execSqlAsync(
              "SELECT 1",
              [cb](const drogon::orm::Result &) {
                json body;
                body["status"] = "ok";
                body["db"] = "ok";
                respondJson(cb, drogon::k200OK, body);
              },
              [cb](const drogon::orm::DrogonDbException &e) {
                json body;
                body["status"] = "degraded";
                body["db"] = "error";
                body["error"] = e.base().what();
                respondJson(cb, drogon::k503ServiceUnavailable, body);
              });
        },
        {drogon::Get});

    // ---------- /orders (POST) ----------
    drogon::app().registerHandler(
        "/orders",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&cb) {
          const auto jsonPtr = req->getJsonObject();
          if (!jsonPtr) {
            Json::Value err(Json::objectValue);
            err["error"] = "Invalid JSON body";
            respondJson(cb, drogon::k400BadRequest, err);
            return;
          }
          const Json::Value &body = *jsonPtr;

          const std::string symbol =
              body.isMember("symbol") && body["symbol"].isString()
                  ? body["symbol"].asString()
                  : "";
          const std::string side =
              body.isMember("side") && body["side"].isString()
                  ? body["side"].asString()
                  : "";
          const double quantity =
              body.isMember("quantity") && body["quantity"].isNumeric()
                  ? body["quantity"].asDouble()
                  : 0.0;
          const double price =
              body.isMember("price") && body["price"].isNumeric()
                  ? body["price"].asDouble()
                  : 0.0;

          if (symbol.empty() || (side != "BUY" && side != "SELL") ||
              quantity <= 0 || price <= 0) {
            Json::Value err(Json::objectValue);
            err["error"] = "Invalid order: symbol, side(BUY/SELL), quantity>0, "
                           "price>0 required";
            respondJson(cb, drogon::k400BadRequest, err);
            return;
          }

          auto db = getDb();
          db->execSqlAsync(
              "INSERT INTO orders(symbol, side, quantity, price, status) "
              "VALUES ($1,$2,$3,$4,'NEW') RETURNING id, created_at",
              [cb, symbol, side, quantity,
               price](const drogon::orm::Result &r) {
                if (r.empty()) {
                  Json::Value error(Json::objectValue);
                  error["error"] = "error";
                  error["detail"] = "Failed to create order";
                  respondJson(cb, drogon::k500InternalServerError, error);
                  return;
                }
                const auto id = r[0]["id"].as<long long>();
                const auto created_at = r[0]["created_at"].as<std::string>();

                Json::Value obj(Json::objectValue);
                obj["id"] = static_cast<Json::Int64>(id);
                obj["symbol"] = symbol;
                obj["side"] = side;
                obj["quantity"] = quantity;
                obj["price"] = price;
                obj["status"] = "NEW";
                obj["created_at"] = created_at;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(obj);
                resp->setStatusCode(drogon::k201Created);
                resp->addHeader("Location", "/orders/" + std::to_string(id));
                cb(resp);
              },
              [cb](const drogon::orm::DrogonDbException &e) {
                Json::Value error(Json::objectValue);
                error["error"] = "Unexpected error";
                error["detail"] = e.base().what();
                respondJson(cb, drogon::k500InternalServerError, error);
              },
              symbol, side, quantity, price);
        },
        {drogon::Post});

    // ---------- /orders (GET) ----------
    drogon::app().registerHandler(
        "/orders",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&cb) {
          int limit = 50;
          if (auto v = req->getParameter("limit"); !v.empty()) {
            try {
              limit = std::clamp(std::stoi(v), 1, 500);
            } catch (...) {
            }
          }
          auto db = getDb();
          db->execSqlAsync(
              "SELECT id, symbol, side, quantity, price, status, created_at "
              "FROM orders ORDER BY id DESC LIMIT $1::INT4",
              [cb](const drogon::orm::Result &r) {
                Json::Value arr(Json::arrayValue);
                for (const auto &row : r) {
                  Json::Value item(Json::objectValue);
                  item["id"] =
                      static_cast<Json::Int64>(row["id"].as<long long>());
                  item["symbol"] = std::string(row["symbol"].c_str());
                  item["side"] = std::string(row["side"].c_str());
                  item["quantity"] = row["quantity"].as<double>();
                  item["price"] = row["price"].as<double>();
                  item["status"] = std::string(row["status"].c_str());
                  item["created_at"] = std::string(row["created_at"].c_str());
                  arr.append(item);
                }
                respondJson(cb, drogon::k200OK, arr);
              },
              [cb](const drogon::orm::DrogonDbException &e) {
                Json::Value body(Json::objectValue);
                body["error"] = "Unexpected error";
                body["detail"] = e.base().what();
                respondJson(cb, drogon::k500InternalServerError, body);
              },
              limit);
        },
        {drogon::Get});

    // ---------- /orders/{id} (DELETE) ----------
    drogon::app().registerHandler(
        "/orders/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&cb, long long id) {
          if (id <= 0) {
            respondJson(cb, drogon::k400BadRequest,
                        Json::Value{{"error", "Invalid id"}});
            return;
          }
          auto db = getDb();
          db->execSqlAsync(
              "DELETE FROM orders WHERE id = $1 RETURNING id",
              [cb](const drogon::orm::Result &r) {
                if (r.empty()) {
                  Json::Value obj(Json::objectValue);
                  obj["error"] = "Order not found";
                  respondJson(cb, drogon::k404NotFound, obj);
                  return;
                }
                Json::Value body(Json::objectValue);
                body["status"] = "deleted";
                body["id"] =
                    static_cast<Json::Int64>(r[0]["id"].as<long long>());
                respondJson(cb, drogon::k200OK, body);
              },
              [cb](const drogon::orm::DrogonDbException &e) {
                Json::Value body(Json::objectValue);
                body["error"] = "Unexpected error";
                body["detail"] = e.base().what();
                respondJson(cb, drogon::k500InternalServerError, body);
              },
              id);
        },
        {drogon::Delete});

  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  drogon::app().setLogLevel(trantor::Logger::kTrace);
  drogon::app().addListener("127.0.0.1", 8080).run();
  return 0;
}
