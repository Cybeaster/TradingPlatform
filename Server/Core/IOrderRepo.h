#pragma once
#include <drogon/orm/DbClient.h>
#include <optional>
#include <string>

// IOrdersRepo.h
struct Order {
  long long id{};
  std::string symbol;
  std::string side; // "buy"/"sell"
  int quantity{};
  double price{};
  std::string status; // "new"/"filled"/...
};

struct IOrdersRepo {
  virtual ~IOrdersRepo() = default;
  virtual Order create(const Order &o, drogon::orm::Transaction &tx) = 0;
  virtual std::optional<Order> get(long long id,
                                   drogon::orm::Transaction &tx) = 0;
  virtual void cancel(long long id, drogon::orm::Transaction &tx) = 0;
};