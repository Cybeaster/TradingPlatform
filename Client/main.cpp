#include <drogon/drogon.h>
#include <json/json.h>
#include <trantor/net/EventLoopThread.h>  // <-- добавь
#include <sstream>                        // <-- для std::istringstream
#include <iostream>
#include <optional>
#include <string>
#include <thread>

using drogon::HttpResponsePtr;

static void printResponse(const std::string& title,
                          drogon::ReqResult result,
                          const HttpResponsePtr& resp) {
  std::cout << "\n=== " << title << " ===\n";
  if (result != drogon::ReqResult::Ok || !resp) {
    std::cout << "Request failed: " << static_cast<int>(result) << "\n";
    return;
  }
  std::cout << "Status: " << static_cast<int>(resp->getStatusCode()) << "\n";
  auto body = resp->getBody();
  std::cout << "Body:\n" << std::string(body.data(), body.length()) << "\n";
}

static std::optional<long long> extractIdFromJson(const std::string& body) {
  Json::CharReaderBuilder b;
  Json::Value j;
  std::string errs;
  std::istringstream iss(body);
  if (!Json::parseFromStream(b, iss, &j, &errs)) return std::nullopt;
  if (j.isMember("id") && j["id"].isInt64()) return j["id"].asInt64();
  return std::nullopt;
}

static void runScenario(const std::string& base) {
  // Запускаем отдельный event loop для клиента
  trantor::EventLoopThread loopThread;
  loopThread.run();

  // Привязываем клиент к этому loop
  auto client = drogon::HttpClient::newHttpClient(base, loopThread.getLoop());
  drogon::app().setLogLevel(trantor::Logger::kTrace);

  // 1) GET /health
  {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/health");
    auto [res, resp] = client->sendRequest(req, 5.0);
    printResponse("GET /health", res, resp);
  }

  long long createdId = 0;
  // 2) POST /orders
  {
    Json::Value order(Json::objectValue);
    order["symbol"] = "AAPL";
    order["side"] = "BUY";
    order["quantity"] = 15.2;
    order["price"] = 120.5;

    auto req = drogon::HttpRequest::newHttpJsonRequest(order);
    req->setMethod(drogon::Post);
    req->setPath("/orders");
    auto [res, resp] = client->sendRequest(req, 5.0);
    printResponse("POST /orders", res, resp);
    if (res == drogon::ReqResult::Ok && resp) {
      auto s = std::string(resp->getBody().data(), resp->getBody().length());
      if (auto idOpt = extractIdFromJson(s)) createdId = *idOpt;
    }
  }

  // 3) GET /orders?limit=10
  {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/orders");
    req->setParameter("limit", "10");
    auto [res, resp] = client->sendRequest(req, 5.0);
    printResponse("GET /orders?limit=10", res, resp);
  }

  // 4) DELETE /orders/{id}
  if (createdId > 0) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Delete);
    req->setPath("/orders/" + std::to_string(createdId));
    auto [res, resp] = client->sendRequest(req, 5.0);
    printResponse("DELETE /orders/{id}", res, resp);
  } else {
    std::cout << "\nNo 'id' extracted from create response; skipping delete\n";
  }

  // 5) GET /orders again
  {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath("/orders");
    req->setParameter("limit", "10");
    auto [res, resp] = client->sendRequest(req, 5.0);
    printResponse("GET /orders?limit=10 (after delete)", res, resp);
  }

  // loopThread корректно остановится в деструкторе
}

int main(int argc, char** argv) {
  const std::string base = (argc > 1) ? argv[1] : "http://127.0.0.1:8080";
  std::thread worker([&] { runScenario(base); });
  worker.join();
  return 0;
}
