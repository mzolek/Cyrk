#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "system.hpp"

namespace {

using namespace std::chrono_literals;

// Prosta maszyna testowa: wytwarza produkty, opcjonalnie z opóźnieniem lub awarią.
class TestMachine : public Machine {
public:
    explicit TestMachine(bool failing = false, std::chrono::milliseconds delay = 0ms)
        : failing(failing), delay(delay) {}

    std::unique_ptr<Product> getProduct() override {
        std::this_thread::sleep_for(delay);
        if (failing) {
            throw MachineFailure();
        }
        produced++;
        return std::make_unique<Product>();
    }

    void returnProduct(std::unique_ptr<Product> product) override {
        if (product) {
            returned++;
        }
    }

    void start() override { started = true; }
    void stop() override { stopped = true; }

    std::atomic<int> produced{0};
    std::atomic<int> returned{0};
    std::atomic<bool> started{false};
    std::atomic<bool> stopped{false};

private:
    bool failing;
    std::chrono::milliseconds delay;
};

System::machines_t makeMachines(std::shared_ptr<TestMachine> burger,
                                std::shared_ptr<TestMachine> fries) {
    return {{"burger", burger}, {"fries", fries}};
}

} // namespace

TEST(SystemTest, MenuListsAllWorkingMachines) {
    auto burger = std::make_shared<TestMachine>();
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 2, 1000);

    auto menu = system.getMenu();
    std::sort(menu.begin(), menu.end());
    EXPECT_EQ(menu, (std::vector<std::string>{"burger", "fries"}));
    EXPECT_TRUE(burger->started);

    system.shutdown();
    EXPECT_TRUE(burger->stopped);
}

TEST(SystemTest, OrderIsFulfilledAndCollected) {
    auto burger = std::make_shared<TestMachine>();
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 2, 1000);

    auto pager = system.order({"burger", "fries", "fries"});
    pager->wait();
    EXPECT_TRUE(pager->isReady());

    auto products = system.collectOrder(std::move(pager));
    EXPECT_EQ(products.size(), 3u);
    EXPECT_EQ(burger->produced, 1);
    EXPECT_EQ(fries->produced, 2);

    auto reports = system.shutdown();
    std::size_t collected = 0;
    for (const auto& report : reports) {
        collected += report.collectedOrders.size();
    }
    EXPECT_EQ(collected, 1u);
}

TEST(SystemTest, InvalidOrdersAreRejected) {
    auto burger = std::make_shared<TestMachine>();
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 1, 1000);

    EXPECT_THROW(system.order({}), BadOrderException);
    EXPECT_THROW(system.order({"pizza"}), BadOrderException);

    system.shutdown();
}

TEST(SystemTest, CollectingTooEarlyThrows) {
    auto burger = std::make_shared<TestMachine>(false, 200ms);
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 1, 100);

    auto pager = system.order({"burger"});
    EXPECT_FALSE(pager->isReady());

    // Nieudana próba odbioru nie może zablokować systemu
    EXPECT_THROW(system.collectOrder(std::move(pager)), OrderNotReadyException);

    system.shutdown();
}

TEST(SystemTest, MachineFailureFailsOrderAndRemovesProductFromMenu) {
    auto burger = std::make_shared<TestMachine>(true);
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 1, 1000);

    auto pager = system.order({"burger"});
    EXPECT_THROW(pager->wait(), FulfillmentFailure);

    EXPECT_EQ(system.getMenu(), (std::vector<std::string>{"fries"}));
    EXPECT_THROW(system.order({"burger"}), BadOrderException);

    auto reports = system.shutdown();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].failedProducts, (std::vector<std::string>{"burger"}));
    EXPECT_EQ(reports[0].failedOrders.size(), 1u);
}

TEST(SystemTest, UncollectedOrderExpiresAndProductsAreReturned) {
    auto burger = std::make_shared<TestMachine>();
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 1, 50);

    auto pager = system.order({"burger", "fries"});
    pager->wait();
    std::this_thread::sleep_for(300ms);

    EXPECT_THROW(system.collectOrder(std::move(pager)), OrderExpiredException);
    EXPECT_EQ(burger->returned, 1);
    EXPECT_EQ(fries->returned, 1);

    auto reports = system.shutdown();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports[0].abandonedOrders.size(), 1u);
}

TEST(SystemTest, NoOrdersAfterShutdown) {
    auto burger = std::make_shared<TestMachine>();
    auto fries = std::make_shared<TestMachine>();
    System system(makeMachines(burger, fries), 2, 1000);

    system.shutdown();

    EXPECT_THROW(system.order({"burger"}), RestaurantClosedException);
    EXPECT_TRUE(system.getMenu().empty());
}

TEST(SystemTest, ManyConcurrentClients) {
    constexpr int kClients = 16;
    constexpr int kWorkers = 4;

    auto burger = std::make_shared<TestMachine>(false, 1ms);
    auto fries = std::make_shared<TestMachine>(false, 1ms);
    System system(makeMachines(burger, fries), kWorkers, 5000);

    std::atomic<int> collectedProducts{0};
    std::vector<std::thread> clients;

    for (int i = 0; i < kClients; i++) {
        clients.emplace_back([&system, &collectedProducts] {
            auto pager = system.order({"burger", "fries"});
            pager->wait();
            auto products = system.collectOrder(std::move(pager));
            collectedProducts += static_cast<int>(products.size());
        });
    }

    for (auto& client : clients) {
        client.join();
    }

    EXPECT_EQ(collectedProducts, 2 * kClients);

    auto reports = system.shutdown();
    std::size_t collected = 0;
    for (const auto& report : reports) {
        collected += report.collectedOrders.size();
    }
    EXPECT_EQ(collected, static_cast<std::size_t>(kClients));
}
