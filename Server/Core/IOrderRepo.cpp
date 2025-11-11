#include "IOrderRepo.h"

// OrdersRepoPg.cpp
class OrdersRepoPg : public IOrdersRepo {
  drogon::orm::DbClientPtr db_;

public:
  explicit OrdersRepoPg(drogon::orm::DbClientPtr db) : db_(std::move(db)) {}

  Order create(const Order &o, drogon::orm::Transaction &tx) override {
    auto r = tx.execSqlSync(
        "INSERT INTO orders(symbol,side,quantity,price,status) "
        "VALUES($1,$2,$3,$4,$5) RETURNING id,symbol,side,quantity,price,status",
        o.symbol, o.side, o.quantity, o.price, o.status);
    const auto &row = r[0];
    return Order{
        row["id"].as<long long>(),     row["symbol"].as<std::string>(),
        row["side"].as<std::string>(), row["quantity"].as<int>(),
        row["price"].as<double>(),     row["status"].as<std::string>()};
  }

  std::optional<Order> get(long long id,
                           drogon::orm::Transaction &tx) override {
    auto r = tx.execSqlSync(
        "SELECT id,symbol,side,quantity,price,status FROM orders WHERE id=$1",
        id);
    if (r.empty())
      return std::nullopt;
    const auto &row = r[0];
    return Order{
        row["id"].as<long long>(),     row["symbol"].as<std::string>(),
        row["side"].as<std::string>(), row["quantity"].as<int>(),
        row["price"].as<double>(),     row["status"].as<std::string>()};
  }

  void cancel(long long id, drogon::orm::Transaction &tx) override {
    tx.execSqlSync("UPDATE orders SET status='canceled' WHERE id=$1", id);
  }
};
