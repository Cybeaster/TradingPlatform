#define NOMINMAX

#include <algorithm>
#include <cstdlib>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <functional>  // std::function
#include <iostream>    // std::cerr
#include <json/json.h> // Json::Value for drogon::newHttpJsonResponse
#include <string>
#include <thread> // std::thread::hardware_concurrency

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

int main(int argc, char *argv[]) {
  try {
    // Порт
    if (const char *portEnv = std::getenv("PORT")) {
      try {
        uint16_t port = 8080;
        int v = std::stoi(portEnv);
        if (v >= 0 && v <= 65535)
          port = static_cast<uint16_t>(v);
      } catch (...) {
      } // дефолт 8080
    }

    // Строка подключения к Postgres (poolsize = кол-ву потоков)
    const char *dbEnv = std::getenv("DATABASE_URL");
    std::string connStr = dbEnv
                              ? std::string(dbEnv)
                              : "postgresql://postgres:postgres@localhost:5432/"
                                "trading?sslmode=disable";

    // Создаем пул соединений к БД (thread-safe)
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    auto db = drogon::orm::DbClient::newPgClient(connStr, threads);

    // --------- /health (GET) ----------
    drogon::app().registerHandler(
        "/health",
        [db](const HttpRequestPtr &,
             std::function<void(const HttpResponsePtr &)> &&callback) {
          db->execSqlAsync(
              "SELECT 1",
              // success
              [callback](const drogon::orm::Result &) {
                json body;
                body["status"] = "ok";
                body["db"] = "ok";
                respondJson(callback, drogon::k200OK, body);
              },
              // error
              [callback](const drogon::orm::DrogonDbException &e) {
                json body;
                body["status"] = "degraded";
                body["db"] = "error";
                body["error"] = e.base().what();
                respondJson(callback, drogon::k503ServiceUnavailable, body);
              });
        },
        {drogon::Get});

    // --------- /orders (POST) : create ----------
    drogon::app().registerHandler(
        "/orders",
        [db](const HttpRequestPtr &req,
             std::function<void(const HttpResponsePtr &)> &&callback) {
          // Parse JSON using Drogon/JsonCpp
          const auto jsonPtr = req->getJsonObject();
          if (!jsonPtr) {
            Json::Value err(Json::objectValue);
            err["error"] = "Invalid JSON body";
            respondJson(callback, drogon::k400BadRequest, err);
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
            respondJson(callback, drogon::k400BadRequest, err);
            return;
          }

          db->execSqlAsync(
              "INSERT INTO orders(symbol, side, quantity, price, status) "
              "VALUES ($1,$2,$3,$4,'NEW') RETURNING id, created_at",
              // success (callbacks first)
              [callback, symbol, side, quantity,
               price](const drogon::orm::Result &r) {
                if (r.empty()) {
                  Json::Value error(Json::objectValue);
                  error["error"] = "error";
                  error["detail"] = "Failed to create order";
                  respondJson(callback, drogon::k500InternalServerError, error);
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
                callback(resp);
              },
              // error
              [callback](const drogon::orm::DrogonDbException &e) {
                Json::Value error(Json::objectValue);
                error["error"] = "Unexpected error";
                error["detail"] = e.base().what();
                respondJson(callback, drogon::k500InternalServerError, error);
              },
              // params (after callbacks)
              symbol, side, quantity, price);
        },
        {drogon::Post});
    // ... existing code ...
    drogon::app().registerHandler(
        "/orders",
        [db](const HttpRequestPtr &req,
             std::function<void(const HttpResponsePtr &)> &&callback) {
          int limit = 50;
          if (auto v = req->getParameter("limit"); !v.empty()) {
            try {
              limit = std::clamp(std::stoi(v), 1, 500);
            } catch (...) {
            }
          }

          db->execSqlAsync(
              "SELECT id, symbol, side, quantity, price, status, created_at "
              "FROM orders ORDER BY id DESC LIMIT $1",
              // success
              [callback](const drogon::orm::Result &r) {
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
                respondJson(callback, drogon::k200OK, arr);
              },
              // error
              [callback](const drogon::orm::DrogonDbException &e) {
                Json::Value body(Json::objectValue);
                body["error"] = "Unexpected error";
                body["detail"] = e.base().what();
                respondJson(callback, drogon::k500InternalServerError, body);
              },
              // params
              limit);
        },
        {drogon::Get});
    // ... existing code ...
    drogon::app().registerHandler(
        "/orders/{1}",
        [db](const HttpRequestPtr &req,
             std::function<void(const HttpResponsePtr &)> &&callback) {
          long long id = 0;
          try {
            id = std::stoll(req->getParameter("1"));
          } catch (...) {
            Json::Value err(Json::objectValue);
            err["error"] = "Invalid id";
            respondJson(callback, drogon::k400BadRequest, err);
            return;
          }

          // Используем RETURNING, чтобы понять, была ли запись
          db->execSqlAsync(
              "DELETE FROM orders WHERE id = $1 RETURNING id",
              // success
              [callback](const drogon::orm::Result &r) {
                if (r.empty()) {
                  Json::Value obj(Json::objectValue);
                  obj["error"] = "Order not found";
                  respondJson(callback, drogon::k404NotFound, obj);
                  return;
                }
                Json::Value body(Json::objectValue);
                body["status"] = "deleted";
                body["id"] =
                    static_cast<Json::Int64>(r[0]["id"].as<long long>());
                respondJson(callback, drogon::k200OK, body);
              },
              // error
              [callback](const drogon::orm::DrogonDbException &e) {
                Json::Value body(Json::objectValue);
                body["error"] = "Unexpected error";
                body["detail"] = e.base().what();
                respondJson(callback, drogon::k500InternalServerError, body);
              },
              // params
              id);
        },
        {drogon::Delete});
  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }
}
// ... existing code ...
